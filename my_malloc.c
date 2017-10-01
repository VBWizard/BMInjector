#include "my_malloc.h"

typedef struct {
    size_t size;
} Metadata;

void *my_malloc(size_t sz) {
    size_t size_with_header = sz + sizeof(Metadata);
    void* pointer = malloc(size_with_header);

    // cast the header into a Metadata struct
    Metadata* header = (Metadata*)pointer;
    header->size = sz;    
    // return the address starting after the header 
    // since this is what the user needs
    return pointer + sizeof(Metadata);
}
