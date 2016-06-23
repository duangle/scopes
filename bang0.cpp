
//------------------------------------------------------------------------------

extern "C" {

typedef struct _Atom Atom;

typedef enum _AtomType {
    AtomType_String = 0,
    AtomType_List = 1
} AtomType;

typedef struct _List {
    Atom *first;
    Atom *last;
} List;

typedef struct _String {
    unsigned long length;
    char *value;
} String;

typedef struct _Atom {
    Atom *up;
    Atom *prev;
    Atom *next;
    int type;
    union {
        String string;
        List list;
    };
} Atom;

Atom *new_string (const char *value, unsigned long length);
Atom *new_string_c (const char *value);

}

//------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

Atom *new_string (const char *value, unsigned long length) {
    Atom *atom = new Atom();
    memset(atom, 0, sizeof(Atom));
    atom->type = AtomType_String;
    atom->string.length = length;
    atom->string.value =
        new char[atom->string.length + 1];
    memcpy(atom->string.value, value, atom->string.length);
    atom->string.value[atom->string.length] = 0;

    return atom;
}

Atom *new_string_c (const char *value) {
    return new_string(value, strlen(value));
}

//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
    Atom *atom = new_string_c("bang!");
    printf("%s\n", atom->string.value);

    return 0;
}
