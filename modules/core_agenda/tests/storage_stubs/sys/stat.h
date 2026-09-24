#pragma once
#define stat agenda_test_stat
#define mkdir agenda_test_mkdir
#ifdef S_ISDIR
#undef S_ISDIR
#endif
struct stat
{
    unsigned st_mode;
};
#define S_ISDIR(mode) (((mode)&040000) != 0)
int stat(const char* path, struct stat* info);
int mkdir(const char* path, unsigned mode);
