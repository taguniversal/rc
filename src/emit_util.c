#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>

#include <sys/stat.h>
#include <errno.h>
#include <string.h>

int mkdir_p(const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = tmp;
    int err = 0;

    for (char *s = tmp + 1; *s; s++)
    {
        if (*s == '/')
        {
            *s = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
            {
                err = errno;
                return err;
            }
            *s = '/';
        }
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
    {
        err = errno;
        return err;
    }

    return 0;
}

void emit_indent(FILE *out, int indent)
{
  for (int i = 0; i < indent; ++i)
    fputc(' ', out);
}

void emit_atom(FILE *out, const char *atom)
{
  // Quote if needed (e.g., contains non-alphanum)
  if (strpbrk(atom, " ()\"\t\n"))
    fprintf(out, "\"%s\"", atom);
  else
    fputs(atom, out);
}