// +------------------------------------------------------------------+
// |             ____ _               _        __  __ _  __           |
// |            / ___| |__   ___  ___| | __   |  \/  | |/ /           |
// |           | |   | '_ \ / _ \/ __| |/ /   | |\/| | ' /            |
// |           | |___| | | |  __/ (__|   <    | |  | | . \            |
// |            \____|_| |_|\___|\___|_|\_\___|_|  |_|_|\_\           |
// |                                                                  |
// | Copyright Mathias Kettner 2017             mk@mathias-kettner.de |
// +------------------------------------------------------------------+
//
// This file is part of Check_MK.
// The official homepage is at http://mathias-kettner.de/check_mk.
//
// check_mk is free software;  you can redistribute it and/or modify it
// under the  terms of the  GNU General Public License  as published by
// the Free Software Foundation in version 2.  check_mk is  distributed
// in the hope that it will be useful, but WITHOUT ANY WARRANTY;  with-
// out even the implied warranty of  MERCHANTABILITY  or  FITNESS FOR A
// PARTICULAR PURPOSE. See the  GNU General Public License for more de-
// ails.  You should have  received  a copy of the  GNU  General Public
// License along with GNU Make; see the file  COPYING.  If  not,  write
// to the Free Software Foundation, Inc., 51 Franklin St,  Fifth Floor,
// Boston, MA 02110-1301 USA.

#include "SectionDF.h"
#include <cstring>
#include <iomanip>
#include "Logger.h"
#include "SectionHeader.h"
#include "WinApiInterface.h"
#include "stringutil.h"
#include "types.h"

namespace {

struct MountPointHandleTraits {
    using HandleT = HANDLE;
    static HandleT invalidValue() { return INVALID_HANDLE_VALUE; }

    static void closeHandle(HandleT value, const WinApiInterface &winapi) {
        winapi.FindVolumeMountPointClose(value);
    }
};

}  // namespace

SectionDF::SectionDF(const Environment &env, Logger *logger,
                     const WinApiInterface &winapi)
    : Section("df", env, logger, winapi,
              std::make_unique<SectionHeader<'\t', SectionBrackets>>("df",
                                                                     logger)) {}

void SectionDF::output_filesystem(std::ostream &out, const std::string &volid) {
    static const int KiloByte = 1024;

    char fsname[128] = {0};
    char volume[512] = {0};
    DWORD dwSysFlags = 0;
    if (!_winapi.GetVolumeInformation(volid.c_str(), volume, sizeof(volume), 0,
                                      0, &dwSysFlags, fsname, sizeof(fsname))) {
        fsname[0] = '\0';  // May be necessary if partial information returned
    }

    ULARGE_INTEGER free_avail, total, free;
    free_avail.QuadPart = 0;
    total.QuadPart = 0;
    free.QuadPart = 0;
    int returnvalue =
        _winapi.GetDiskFreeSpaceEx(volid.c_str(), &free_avail, &total, &free);
    if (returnvalue > 0) {
        double perc_used = 0;
        if (total.QuadPart > 0)
            perc_used = 100 - (100 * free_avail.QuadPart / total.QuadPart);

        std::string volumeStr{volume};
        if (!volumeStr.empty())  // have a volume name
            std::replace(volumeStr.begin(), volumeStr.end(), ' ', '_');
        else
            volumeStr = volid;

        out << volumeStr << "\t" << fsname << "\t"
            << (total.QuadPart / KiloByte) << "\t"
            << (total.QuadPart - free_avail.QuadPart) / KiloByte << "\t"
            << (free_avail.QuadPart / KiloByte) << "\t" << std::fixed
            << std::setprecision(0) << perc_used << "%\t" << volid << "\n";
    }
}

void SectionDF::output_mountpoints(std::ostream &out,
                                   const std::string &volid) {
    char mountpoint[512];
    WrappedHandle<MountPointHandleTraits> hPt{
        _winapi.FindFirstVolumeMountPoint(volid.c_str(), mountpoint,
                                          sizeof(mountpoint)),
        _winapi};

    if (hPt) {
        while (true) {
            const std::string combined_path = volid + mountpoint;
            output_filesystem(out, combined_path);
            if (!_winapi.FindNextVolumeMountPoint(hPt.get(), mountpoint,
                                                  sizeof(mountpoint)))
                break;
        }
    }
}

bool SectionDF::produceOutputInner(std::ostream &out,
                                   const std::optional<std::string> &) {
    Debug(_logger) << "SectionDF::produceOutputInner";
    char buffer[4096];
    DWORD len = _winapi.GetLogicalDriveStrings(sizeof(buffer), buffer);

    char *end = buffer + len;
    char *drive = buffer;
    while (drive < end) {
        UINT drvType = _winapi.GetDriveType(drive);
        if (drvType == DRIVE_FIXED)  // only process local harddisks
        {
            output_filesystem(out, drive);
            output_mountpoints(out, drive);
        }
        drive += strlen(drive) + 1;
    }

    // Output volumes, that have no drive letter. The following code
    // works, but then we have no information about the drive letters.
    // And if we run both, then volumes are printed twice. So currently
    // we output only fixed drives and mount points below those fixed
    // drives.

    // HANDLE hVolume;
    // char volid[512];
    // hVolume = FindFirstVolume(volid, sizeof(volid));
    // if (hVolume != INVALID_HANDLE_VALUE) {
    //     df_output_filesystem(out, volid);
    //     while (true) {
    //         // df_output_mountpoints(out, volid);
    //         if (!FindNextVolume(hVolume, volid, sizeof(volid)))
    //             break;
    //     }
    //     FindVolumeClose(hVolume);
    // }

    return true;
}
