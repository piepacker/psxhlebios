#include "psdisc-filesystem.h"
#include "psxhle-filesystem.h"
#include "psdisc-cdvd-image.h"

#include "icy_assert.h"
#include "icy_log.h"
#include "jfmt.h"
#include "fs.h"
#include "posix_file.h"
#include "defer.h"

#define HLE_MEDNAFEN_IFC 0
#define HLE_PCSX_IFC     1


// verbose information is logged to stderr to avoid corrupting stdout behavior.
// (stdout information may be used by other scripts in automation pipeline)
static bool g_bVerbose = 0;

struct fileEnt_t
{
    psdisc_off_t    start_sector;
    psdisc_off_t    parent_sector;
    psdisc_off_t    len_bytes;
    int             type;
    char            name[kPsDiscMaxFileNameLength+1];

    bool isRoot() const {
        return parent_sector == 0;
    }
};

// FilesBySectorLUT allows indexing file information quickly according to sector seek position.
// useful for reverse-lookup of current file being read by an emulator.

using FilesBySectorLUT      = std::map <psdisc_off_t,fileEnt_t>;
using FilesByFullpathLUT    = std::map <fs::path,fileEnt_t>;
using DirsBySectorLUT       = std::map <psdisc_off_t,fs::path>;

FilesBySectorLUT      m_filesBySector;
FilesByFullpathLUT    m_filesByFullpath;
DirsBySectorLUT       m_dirsBySector;

// currently must be done as a separate pass, since AddFile may not be called in dir-followed-by-files order.
void buildFilesByDirLUT()
{
    for(const auto& item : m_filesBySector) {
        auto& fe = item.second;
        auto dir = m_dirsBySector[fe.parent_sector] / fe.name;
        m_filesByFullpath.insert({dir, fe});
    }
}

void recurse_parent_walk(fs::path& dest, psdisc_off_t parent) {
    const auto& psec = m_filesBySector[parent];
    if (psec.parent_sector) {
        recurse_parent_walk(dest, psec.parent_sector);
    }
    if (psec.name[0]) {
        dest /= psec.name;
    }
}

void AddFile(psdisc_off_t secstart, psdisc_off_t len, int type, const uint8_t* name, int nameLen, psdisc_off_t parent)
{
    dbg_check( nameLen <= kPsDiscMaxFileNameLength );

    if (g_bVerbose) {
        log_error( "(psxfs) AddFile [parent=%-6jd sector=%-6jd len=%-10jd]: %s",
            JFMT(parent), JFMT(secstart), JFMT(len), name
        );
    }

    if (m_filesBySector.count(secstart)) {
        log_error("(psxfs) Suspicious duplicate encountered [parent=%-6jd sector=%-6jd len=%-10jd]: %s",
            JFMT(parent), JFMT(secstart), JFMT(len), name
        );

        return;
    }

    fileEnt_t fe;

    fe.start_sector     = secstart;
    fe.parent_sector    = parent;
    fe.len_bytes        = len;
    fe.type             = type;
    memcpy(fe.name, name, nameLen);
    fe.name[nameLen] = 0;

    // strip the ECMA-119 semicolon revision info.
    if (fe.name[nameLen-2] == ';') {
        fe.name[nameLen-2]  = 0;
    }

    // allows indexing file information quickly according to sector seek position.
    // useful for reverse-lookup of current file being read by an emulator.
    auto seclen = (len + 2047) / 2048;
    for (auto seci=secstart; seci<secstart+seclen; ++seci) {
        m_filesBySector.insert({seci, fe});
    }

    if (type == FILETYPE_DIR) {
        fs::path dirdest;
        recurse_parent_walk(dirdest, secstart);
        m_dirsBySector.insert({secstart, dirdest});
    }
}

#if HLE_MEDNAFEN_IFC
#include "mednafen/cdrom/cdromif.h"
extern CDIF* GetCurrentCDIF();
CDIF* s_cur_cdif;

void psxFs_CacheFilesystem() {

    auto cdif = GetCurrentCDIF();

    // pointer comparison, not my ideal choice, but the cdif doesn't give us much internal data from
    // which to further identify the media from another media.
    if (cdif == s_cur_cdif) {
        return;
    }

    m_filesBySector   .clear();
    m_dirsBySector    .clear();
    m_filesByFullpath .clear();

    m_dirsBySector.insert({0, fs::path()});

    s_cur_cdif = cdif;

    PsDiscDirParser parser;
    parser.read_data_cb = [&](uint8_t* dest, psdisc_off_t sector, psdisc_off_t offset, psdisc_off_t length) {
        dbg_check(offset == 0);
        return s_cur_cdif->ReadSector(dest, sector, (length + 2047) / 2048) != 0;
    };      
    parser.ReadFilesystem(AddFile);
    buildFilesByDirLUT();
}
#endif

#if HLE_PCSX_IFC

#include "plugins.h"

static int s_fd = -1;
static MediaSourceDescriptor s_media;
static PsDisc_IO_Interface s_ioifc;

static bool ReadData2048(void* dest, psdisc_off_t sector, psdisc_off_t offset, psdisc_off_t length) {
    dbg_check(dest);

    if (!length)  return 1;
    if (!dest)    return 0;

    auto sec_read_count = (length + 2047) / 2048;

    if ((sector + sec_read_count) > s_media.num_sectors) {
        log_host("ERROR: ReadData2048(sector=%jd, len=%jd): read past end of media", JFMT(sector), JFMT(length));
        return 0;
    }


    auto read_offset = sector * s_media.sector_size;

    read_offset += s_media.offset_file_header;
    read_offset += s_media.offset_sector_leadin;      // data can skip the sync pattern, address, and mode info

    if (s_media.sector_size == 2048) {
        // optimized fastpath for ISO media.
        auto res = s_ioifc.pread_cb(dest, s_media.sector_size * sec_read_count, read_offset);
            if(!res) {
                log_host("ERROR: ReadData2048(seekpos=%jd)", JFMT(read_offset));
                return false;
            }
    }
    else {
        auto*   wptr = (uint8_t*)dest;
        auto    end_sector = sector + sec_read_count;

        for(;sector < end_sector; ++sector,
            read_offset += s_media.sector_size,
            wptr        += 2048
        ) {
            auto res = s_ioifc.pread_cb(wptr, 2048, read_offset);
            if(!res) {
                log_host("ERROR: ReadData2048(seekpos=%jd)", JFMT(read_offset));
                return false;
            }
        }
    }

    return true;
}

std::string s_curfilename;

void psxFs_CacheFilesystem() {

    auto filename = GetIsoFile();

    if (s_fd >= 0) {
        if (strcasecmp(filename, s_curfilename.c_str()) == 0) {
            return;
        }
        posix_close(s_fd);
    }
    s_curfilename = filename;
    log_host("(psxhle) caching iso filesystem: %s", filename);

    s_fd = posix_open(filename, O_RDONLY, _S_IREAD | _S_IWRITE);
    if (s_fd < 0) {
        log_error("%s: %s", strerror(errno), filename);
        dbg_abort();
    }

    s_ioifc = {
        // pread
        [&](void* dest, intmax_t count, intmax_t pos) {
            dbg_check(s_fd >= 0);
            return posix_pread(s_fd, dest, count, pos);
        }
    };

    if (!DiscFS_DetectMediaDescription(s_media, s_fd)) {
        log_error("Could not parse contents of file: %s", filename);
        dbg_abort();
    }

    m_filesBySector   .clear();
    m_dirsBySector    .clear();
    m_filesByFullpath .clear();

    m_dirsBySector.insert({0, fs::path()});

    PsDiscDirParser parser;
    parser.read_data_cb = ReadData2048; 
    parser.ReadFilesystem(AddFile);
    buildFilesByDirLUT();
}
#endif

fs::path psFs_Canonalize(const char* src) {
    if (!src) return {};

    // skip rooted slash. All paths are assumed to be rooted.
    // (there is no CWD mechanic within the psFs)

    if (src[0] == '/') ++src;

    auto result = fs::path(src);
    auto& uni = result.raw_modifiable_uni();
    auto len = uni.length();
    if (len < 3) return result;
    if (uni[len-2] == ';') {
        result.raw_modifiable_uni().resize(len-2);
        result.raw_commit_modified();
    }
    return result;
}

bool psxFs_ReadSectorData2048(void* dest,  psdisc_sec_t sector, int nSectors) {
    psxFs_CacheFilesystem();
#if HLE_PCSX_IFC
    return ReadData2048(dest, sector, 0, nSectors * 2048);
#endif
#if HLE_MEDNAFEN_IFC
    return s_cur_cdif->ReadSector((uint8_t*)dest, sector, nSectors) != 0;
#endif
}

// Result from this read can be fed directly into CDIF::ReadSector() by caller.
// returns 0 on failure (sector 0 is never a valid position for a cdrom file).
psdisc_sec_t psxFs_GetFileSector(const char* path) {
    auto canon = psFs_Canonalize(path);
    if (auto it = m_filesByFullpath.find(canon); it != m_filesByFullpath.end()) {
        return it->second.start_sector;
    }
    return 0;
}

bool psxFs_LoadFile(const char* path, std::vector<uint8_t>& dest) {
    auto canon = psFs_Canonalize(path);

    log_host("Here it is:");
    log_host(" > %s", path);
    log_host(" > %s", canon.uni_string().c_str());

    if (auto it = m_filesByFullpath.find(canon); it != m_filesByFullpath.end()) {
        auto& item = it->second;
        auto len_in_sectors = (item.len_bytes + 2047) / 2048;
        dest.resize(len_in_sectors * 2048);
        auto* dptr = dest.data();

#if HLE_PCSX_IFC
        auto read_result = ReadData2048(dest.data(), item.start_sector, 0, len_in_sectors * 2048);
        dbg_check(read_result);
#endif
#if HLE_MEDNAFEN_IFC
        auto read_result = s_cur_cdif->ReadSector(dest.data(), item.start_sector, len_in_sectors);
        dbg_check(read_result != 0);
#endif
        return read_result;
    }
    return 0;
}

bool psxFs_LoadExecutableHeader(const char* path, PSX_EXE_HEADER& dest) {
    psxFs_CacheFilesystem();
    auto canon = psFs_Canonalize(path);

    log_host("Here it is:");
    log_host(" > %s", path);
    log_host(" > %s", canon.uni_string().c_str());

    if (auto it = m_filesByFullpath.find(canon); it != m_filesByFullpath.end()) {
        auto& item = it->second;
        //log_host(" > sector = %jd", JFMT(it->second.start_sector));
#if HLE_PCSX_IFC
        auto read_result = ReadData2048(&dest, item.start_sector, 0, 1 * 2048);
        dbg_check(read_result);
#endif
#if HLE_MEDNAFEN_IFC
        auto read_result = s_cur_cdif->ReadSector(&dest, item.start_sector, 1);
        dbg_check(read_result != 0);
#endif
        return read_result;
    }

    return 0;
}
