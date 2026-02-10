/*
    Here we put the mechanisms we have planned for freeing and managing free'd chunks


    - use madvise only unless whole segment is freeable to OS
        - gives UAF protection on unused pages. (will segfault)

    - trace all free blocks per size so that we can improve cache locality for commonly used chunk sizes. 
        - A single cache line (64 bytes = 512 bits) can track 512 slots.
    
    - reference counting into page ranges == used for garbage collection ~~, reduces RSS

    - encode all free page pointers using the segment key(found in heap metadata?) we can AND it w/ lower bits of a pointer
        - should be doable in constant time since we can find segment in const time and then we store that chunk metadata masked!
        - if an attacker overwrites free page pointer, whenever it is allocated it will be xor'd again, and will no longer point to a valid page in the segment. similar to heap keys in glibc
        - ASAN like - we can use a key to pattern free chunk metadata and crash if overwritten

    - NX free pages. (Write XOR Execute) applied to all other pages. 

    - non multi-threaded pages: For chunks in a given page, randomly allocate. 
        we can keep a bitmap of unused chunks and use a PRNG modded by the # of chunks 
        left to calculate which to allocate.
    
    - multi-threaded pages: do lifo so we can improve cache locality 
        - freeing cross-thread shall be deffered, it will track it's own private list. 

*/