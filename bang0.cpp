
//------------------------------------------------------------------------------

extern "C" {

typedef struct _BangAtom BangAtom;

typedef enum _BangAtomType {
    BangAtomType_String = 0,
    BangAtomType_List = 1
} BangAtomType;

typedef struct _BangList {
    BangAtom *first;
    BangAtom *last;
} BangList;

typedef struct _BangString {
    unsigned long length;
    char *value;
} BangString;

typedef struct _BangAtom {
    BangAtom *up;
    BangAtom *prev;
    BangAtom *next;
    int type;
    union {
        BangString string;
        BangList list;
    };
} BangAtom;

void bang_init ();
void bang_exit ();
BangAtom *bang_new_string (const char *value, unsigned long length);
BangAtom *bang_new_string_c (const char *value);
BangAtom *bang_new_list ();
void bang_append (BangAtom *list, BangAtom *atom);
void bang_delete (BangAtom *atom);
const char *bang_string (BangAtom *atom);
unsigned long bang_length (BangAtom *atom);
BangAtom *bang_first (BangAtom *atom);
BangAtom *bang_last (BangAtom *atom);
BangAtom *bang_prev (BangAtom *atom);
BangAtom *bang_next (BangAtom *atom);
BangAtom *bang_up (BangAtom *atom);
int bang_type (BangAtom *atom);
int bang_islist (BangAtom *atom);
int bang_isstring (BangAtom *atom);

}

//------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <assert.h>

//------------------------------------------------------------------------------

static void detach(BangAtom *atom) {
    if (atom->up) {
        if (atom->prev) {
            atom->prev->next = atom->next;
        } else {
            atom->up->list.first = atom->next;
        }

        if (atom->next) {
            atom->next->prev = atom->prev;
        } else {
            atom->up->list.last = atom->prev;
        }

        atom->up = NULL;
        atom->prev = NULL;
        atom->next = NULL;
    }
}

//------------------------------------------------------------------------------

void bang_init () {
}

void bang_exit () {
}

BangAtom *bang_new_string (const char *value, unsigned long length) {
    BangAtom *atom = new BangAtom();
    memset(atom, 0, sizeof(BangAtom));
    atom->type = BangAtomType_String;
    atom->string.length = length;
    atom->string.value =
        new char[atom->string.length + 1];
    memcpy(atom->string.value, value, atom->string.length);
    atom->string.value[atom->string.length] = 0;

    return atom;
}

BangAtom *bang_new_string_c (const char *value) {
    return bang_new_string(value, strlen(value));
}

BangAtom *bang_new_list () {
    BangAtom *atom = new BangAtom();
    memset(atom, 0, sizeof(BangAtom));
    atom->type = BangAtomType_List;
    return atom;
}

void bang_append (BangAtom *list, BangAtom *atom) {
    assert(list->type == BangAtomType_List);
    detach(atom);

    atom->up = list;
    atom->prev = list->list.last;
    atom->next = NULL;

    if (list->list.last) {
        list->list.last->next = atom;
    } else {
        list->list.first = atom;
    }

    list->list.last = atom;
}

void bang_delete (BangAtom *atom) {
    detach(atom);
    switch(atom->type) {
        case BangAtomType_String: {
            delete atom->string.value;
        } break;
        case BangAtomType_List: {
            while (atom->list.first) {
                bang_delete(atom->list.first);
            }
        } break;
        default: {
            assert(false && "unknown type");
        } break;
    }

    // overwrite with crap
    memset(atom, 0xcd, sizeof(BangAtom));
    delete atom;
}

const char *bang_string (BangAtom *atom) {
    assert(atom->type == BangAtomType_String);
    return atom->string.value;
}

unsigned long bang_length (BangAtom *atom) {
    assert(atom->type == BangAtomType_String);
    return atom->string.length;
}

BangAtom *bang_first (BangAtom *atom) {
    assert(atom->type == BangAtomType_List);
    return atom->list.first;
}

BangAtom *bang_last (BangAtom *atom) {
    assert(atom->type == BangAtomType_List);
    return atom->list.last;
}

BangAtom *bang_prev (BangAtom *atom) {
    return atom->prev;
}

BangAtom *bang_next (BangAtom *atom) {
    return atom->next;
}

BangAtom *bang_up (BangAtom *atom) {
    return atom->up;
}

int bang_type (BangAtom *atom) {
    return atom->type;
}

int bang_islist (BangAtom *atom) {
    return (atom->type == BangAtomType_List)?1:0;
}

int bang_isstring (BangAtom *atom) {
    return (atom->type == BangAtomType_String)?1:0;
}

//------------------------------------------------------------------------------

int main(int argc, char ** argv) {
    bang_init();

    BangAtom *atom0 = bang_new_string_c("bang!");
    BangAtom *atom1 = bang_new_string_c("bang!!");
    BangAtom *list = bang_new_list();
    bang_append(list, atom0);
    bang_append(list, atom1);

    printf("%s %s\n", list->list.first->string.value, list->list.first->next->string.value);
    bang_delete(list);

    bang_exit();

    return 0;
}
