#ifndef CL_H
#define CL_H

#include <inttypes.h>
#include <stdlib.h>
#include <time.h>


typedef uint8_t cell;
/* Bit 0: S0   
 * Bit 1: S1                 
 * Bit 2..7: reserved                
 *      
 */

typedef uint8_t rule_t;

/* 128-bit vector */
typedef struct vec128b_t { 
  uint8_t b [16]; 
} vec128b_t;

/* 128-cell vector */
typedef struct vec128c_t {
  cell c [16 * 8]; 
} vec128c_t;

/* 128 + 1 -cell vector */   // big-endian, index 0 is reserved, 1-based indexing 
                             // acccess with get_cell set_cell
typedef struct vec128bec_t {
  cell c [(16 * 8) + 1]; 
} vec128bec_t;

vec128bec_t* vec_alloc(void);

/* Little-endian version for display */
typedef struct vec128lec_t {
  cell c [(16 * 8) + 1]; 
} vec128lec_t;

typedef struct frame_t {
    size_t count;
    vec128bec_t* rows[128];
} frame_t;

frame_t* frame_alloc(void);
void frame_free(frame_t* f);
void frame_clear(frame_t* f);


void frame_pop (frame_t* f, vec128bec_t* v);
size_t frame_push(vec128bec_t* v, frame_t* f);

void frame_get_row(frame_t* f, uint8_t row, vec128bec_t* v);
void frame_put_row(frame_t* f, uint8_t row, vec128bec_t* v);

typedef enum {CELL_NIL, CELL_TRUE, CELL_FALSE, CELL_NULL} cell_state_t;

uint8_t vecbe_get_byte(uint8_t byte_num, vec128bec_t* v);
uint32_t vecbe_hamming_weight (vec128bec_t* vec);
uint8_t vecbe_pack(uint8_t* bytes, vec128bec_t* v);
cell get_cell(vec128bec_t* v, uint8_t index);
uint8_t set_cell(vec128bec_t* v, uint8_t index, cell c);

void vcopy (vec128bec_t* from, vec128bec_t* to);
void vmov (vec128bec_t* from, vec128bec_t* to);
void vxfer (vec128bec_t* from, vec128bec_t* to);
void vset (vec128bec_t* v, cell c);
void vsetN(vec128bec_t* v);
void vsetZ(vec128bec_t* v);

cell cxor (cell left, cell right);
cell cor (cell left, cell right);
cell cand (cell left, cell right);

cell binary_to_cell (char c);
char cell_to_binary (cell c);

void halt(char* msg);

#endif


#ifndef FV_H
#define FV_H

#define NUM_FORMATS           14

#define FMT_VEC_BINARY        0
#define FMT_VEC_SHA1          1
#define FMT_VEC_BINARY_TEXT   2
#define FMT_VEC_IPV4          4
#define FMT_VEC_GUID          5
#define FMT_VEC_IPV6          6
#define FMT_VEC_PSI           8
#define FMT_VEC_INT32         10
#define FMT_VEC_UUID          11
#define FMT_VEC_BASE64        12

char* fmt_vecbe(vec128bec_t* v, int fmt_type);
char cell_to_char(cell c);
char* frame_to_str(frame_t* f, int fmt_type);

#endif

#ifndef CMD_H
#define CMD_H

extern int verbose_flag;

#endif

#ifndef ML_H
#define ML_H



typedef enum {CP_NIL, CP_NULL, CP_IDLE, CP_RUN, CP_HALT} cp_state_t;

typedef struct cell_proc_t {
  frame_t* Stack;
  int counter_mode;   // With external seed
  cp_state_t s;       // private state - make 64
  size_t  index;      // cell row index
  vec128bec_t* A;      // GP Input 1
  vec128bec_t* B;      // GP Input 2
  vec128bec_t* C;      // GP Buffer - will automatically be updated with D in next cycle
  vec128bec_t* D;      // GP Output (Next State)
  vec128bec_t* PSI;    // Timestamp                                     Synchronized with other cells on row
  vec128bec_t* R30;    // Deterministic randomness                      Synchronized with other cells on row
  vec128bec_t* SDR30;  // Seed for R30INC
  vec128bec_t* SDTIME; // Time Seed
  vec128bec_t* R;      // Free variable (non-deterministic randomness)  Synchronized with other cells on row ?
  vec128bec_t* NR;     // Next Row pointer
  vec128bec_t* CR;     // Current Row pointer
} cell_proc_t;

typedef struct machine_t {
    cell_proc_t cells[128];
} machine_t;

typedef cell (*rulefunc)(cell, cell, cell);
cell rule (cell left, cell middle, cell right);

/* 96-bit entropy seed containing RTC seconds, microseconds, and an internal cycle count  */
typedef struct TimeSeed {
  uint32_t   long_count;
  uint32_t   short_count;
  uint32_t   cyclic;
} TimeSeed;

TimeSeed time_seed(void);
char* timeseed_tostr(TimeSeed* ts);
vec128bec_t* time_seed_to_vec(TimeSeed seed);
int check_clocks(void);

uint16_t cp_init(void);
void cp_reset(struct cell_proc_t* cp);
void cp_halt(struct cell_proc_t* cp, char* msg);
void cp_free(struct cell_proc_t* cp);
void cp_setstate(struct cell_proc_t * cp, cp_state_t st);
cp_state_t cp_getstate(struct cell_proc_t* cp);
void show_cp(cell_proc_t* cp);

void mi0_xor  (cell_proc_t* cp);
void mi0_incSDTIME (cell_proc_t*  cp);
void mi1_incR30 (cell_proc_t*  cp);
void mi2_sha30 (cell_proc_t*  cp);
void mi2_genR (cell_proc_t*  cp);
void mi2_incPSI (cell_proc_t*  cp);

void pushSD(cell_proc_t* cp);
void popSD(cell_proc_t* cp);
void pushGP(cell_proc_t* cp);
void popGP(cell_proc_t* cp);

/* During Time Quantum :
   I/O input transfers occur (A,B loaded), get_neighbor calls resolved 
   PSI is atomically updated
   R30 is atomically advanced by 128 bits
   R is atomically updated based on PSI
   1 Process Cycle occurs
   I/O output transfers occur
 */
void mi5_time_quantum(cell_proc_t* cp);
int seed_str_to_vec(const char* seed_str, vec128bec_t* out_vec);

// API
char* new_block(void);
struct in6_addr mkrand_generate_ipv6(void) ;
#endif



