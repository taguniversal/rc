/*                                 
 * Cell Logic
 * Three state logic functions and vector operations
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, TAG Universal Machine.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <getopt.h>
#include <limits.h>
#include <errno.h>
#include "mkrand.h"
#include <math.h>
#include <inttypes.h>

#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>

// Logging macros
#define COLOR_RESET   "\x1b[0m"
#define COLOR_INFO    "\x1b[36m"
#define COLOR_WARN    "\x1b[33m"
#define COLOR_ERROR   "\x1b[31m"

#define LOG(level, color, fmt, ...) do { \
    time_t now = time(NULL); \
    struct tm *tm_info = localtime(&now); \
    char time_buf[20]; \
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info); \
    if (isatty(fileno(stderr))) { \
        fprintf(stderr, "%s[%s] [%s] " fmt "%s\n", color, time_buf, level, ##__VA_ARGS__, COLOR_RESET); \
    } else { \
        fprintf(stderr, "[%s] [%s] " fmt "\n", time_buf, level, ##__VA_ARGS__); \
    } \
} while (0)

#define LOG_INFO(fmt, ...)  LOG("INFO",  COLOR_INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  LOG("WARN",  COLOR_WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG("ERROR", COLOR_ERROR, fmt, ##__VA_ARGS__)


/* Flag set by ‘--verbose’. */
int verbose_flag = 0;

int profile_flag = 0;

void halt(char* msg)
{
  LOG_ERROR("HALT: %s\n",msg);
  exit(EXIT_FAILURE); 
}

/* Allocate vector memory slot */
vec128bec_t* vec_alloc(void) { 
  vec128bec_t* r = malloc(sizeof(vec128bec_t));
  if (r) {
    vset(r, CELL_NULL);
  }

  return (r);
}

/* Allocate frame pointer and vector memory slots */
frame_t* frame_alloc(void) {
  int i;
  frame_t* f = malloc(sizeof(frame_t));

  if (f) {
    for (i=1; i<= 128; i++){
      (f->rows[i-1]) = vec_alloc();
    }
   f->count = 0;
  }
  
  return (f);
}

/* Deallocate vector memory slots and frame pointer */
void frame_free(frame_t* f) {
  size_t i;

  for (i=1; i <= 128; i++){
    free(f->rows[i-1]);
  }

  free(f);
}

/* Nullify vector memory slots
   C - Does not deallocate memory
 */
void frame_clear(frame_t* f){
  size_t i;

  for (i=1; i <= 128; i++){
    vsetN(f->rows[i-1]);
  }

  f->count = 0;
}

/* Copy vector from given frame slot
 * C - no memory operations
 */
void frame_get_row(frame_t* f, uint8_t row, vec128bec_t* v){
  if ((row < 1) || (row > 128)) { halt ("frame_get_row: bad index"); }
  
  vcopy (f->rows[row-1], v);
}

/* Copy vector into frame slot at given index 
   C - no memory operations
 */
void frame_put_row(frame_t* f, uint8_t row, vec128bec_t* v){
  if ((row < 1) || (row > 128)) { halt ("frame_put_row: bad index"); }

  vcopy(v, f->rows[row-1]);
} 

/* Pop vector from stack and place in v  */
void frame_pop (frame_t* f, vec128bec_t* v){
  if (f->count == 0) { return; }
  vmov (f->rows[(f->count)-1], v);
  f->count -= 1;
}

/* Copies vector into topmost frame memory slot, nullifying source 
   C - no memory operations
 */
size_t frame_push(vec128bec_t* v, frame_t* f){
  f->count += 1;
  vmov(v, f->rows[(f->count)-1]);
  return (f->count);
}


/* GetCell - Vectors are Big-Endian and 1-based. C Index 0 is reserved and not exposed. */
cell get_cell(vec128bec_t* v, uint8_t index){
  if ((index < 1) || (index > 128)) { halt ("get_cell : Bad Index"); }

  /* Index Mapping
     Cell Vector : [RES] [128] [127] .. [1] 
     C Array     : [0]   [1]   [2]   .. [128]
   */

  return (v->c[129-index]); 
}

uint8_t set_cell(vec128bec_t* v, uint8_t index, cell c){
  if ((index < 1) || (index > 128)) { halt ("set_cell : Bad Index"); } 
  if (v == NULL) {
    LOG_ERROR("Null Vector[%d]\n",index);
  }
  v->c[129-index] = c;
  return(0);
}

/* VSET - Set vector to given value */
void vset (vec128bec_t* v, cell c){
  size_t i;

  for (i=1; i<=128; i++){
     set_cell(v,i,c);
  }
}

/* Copy vector, leaving source intact */
void vcopy (vec128bec_t* from, vec128bec_t* to) {
  size_t i;

  for (i=1; i<=128; i++){
     set_cell(to,i,get_cell(from, i));
  }
}

/* Move vector, nullifying source 
 * C - no memory operations */
void vmov (vec128bec_t* from, vec128bec_t* to) {
  vcopy(from, to);
  vsetN(from);
}

/* Move vector, freeing source memory (von Neumann) */
void vxfer (vec128bec_t* from, vec128bec_t* to) {
  vcopy(from, to);
  free(from);
}

/* Set vector to 00.. */
void vsetZ(vec128bec_t* v) {
  vset(v, CELL_FALSE);
}

/* Set register to NULL
 * C - no memory operations
 * Synchronization/wavefront actions hooked here
 */
void vsetN(vec128bec_t* v){
  vset(v, CELL_NULL);
}


/* Extract Byte from Vec. Byte index 1..16 */
uint8_t vecbe_get_byte(uint8_t byte_num, vec128bec_t* v){
  uint8_t i;
  uint8_t r = 0;
  uint8_t b = 0;
  uint8_t offset = ((byte_num-1) * 8) +1;
  /*
     byte_num  offset
     1         ((1-1) * 8) +1 = 1
     2         ((2-1) * 8) +1 = 9
     3         ((3-1) * 8) +1 = 17
     4         ((4-1) * 8) +1 = 25
     5 
     6
     7
     8
     9
     10
     11
     12
     13
     14
     15
     16
  */

  if ((byte_num < 1) || (byte_num > 16)) { halt("get_byte: invalid index"); }

  for (i = offset; i<= (offset + 7); i++){
    b = (get_cell(v,i) == CELL_TRUE) ? 1 : 0;
    r = r | (b << (i-offset));  
  }
  return(r);
}

/*  TODO change signature on this and vecbe_get_byte to vecotr, bytenum, val */
uint8_t vecbe_set_byte(uint8_t byte_num, uint8_t byte_val, vec128bec_t* v) {
  uint8_t i;
  uint8_t r = 0;
  uint8_t b = 0;
  uint8_t offset = ((byte_num-1) * 8) +1;

  if ((byte_num < 1) || (byte_num > 16)) { halt("set_byte: invalid index"); } 

  for (i = offset; i<= (offset + 7); i++){
    b = (get_cell(v,i) == CELL_TRUE) ? 1 : 0;
    r = r | (b << (i-offset));  
  }
  return(r);

}

uint8_t vecbe_pack(uint8_t* bytes, vec128bec_t* v)
{
  for (int i = 0; i<16; i++) {
    bytes[i] = vecbe_get_byte(i+1, v);
  }
  return(16);
}

uint32_t vecbe_hamming_weight (vec128bec_t* v) {
  int i = 0;
  uint32_t w = 0;
  for (i=1; i<=128; i++){
    if (get_cell(v, i) == CELL_TRUE) {
      w += 1;
    }
  }
  return(w);  
}


/* AND */
cell cand (cell left, cell right) 
{
   if ((left == CELL_NULL) || (right == CELL_NULL)) {
      return (CELL_NULL);
   } else {
      if ((left == CELL_TRUE) && (right == CELL_TRUE)) {
         return (CELL_TRUE);
      } else {
         return (CELL_FALSE);
      }
   }
}

/* OR */
cell cor (cell left, cell right)
{
   if ((left == CELL_NULL) || (right == CELL_NULL)) {
      return (CELL_NULL);
   } else {
      if ((left == CELL_TRUE) || (right == CELL_TRUE)) {
         return (CELL_TRUE);
      } else {
         return (CELL_FALSE);
      }
   }
}


/* XOR */
cell cxor (cell left, cell right)
{
   if ((left == CELL_NULL) || (right == CELL_NULL)) {
      return (CELL_NULL);
   } else {
      if (((left == CELL_TRUE)  && (right == CELL_FALSE)) ||
          ((left == CELL_FALSE) && (right == CELL_TRUE))) {
         return (CELL_TRUE);
      } else {
         return (CELL_FALSE);
      }
   }
}


char cell_to_binary (cell c) {
    char r = 0;

    switch (c)
      {
         case CELL_FALSE : r = 0;
                  break;

         case CELL_TRUE  : r = 1;
                  break;

         case CELL_NULL  : halt("cell_to_binary - NULL cell");
                  break;

         default : halt("cell_to_binary - invalid cell value");

      }

      return(r);
}

cell binary_to_cell (char c) {
    char r = CELL_FALSE;

    switch (c)
      {
         case 0  : r = CELL_FALSE;
                  break;

         case 1  : r = CELL_TRUE;
                  break;

         default : halt("binary_to_cell - invalid binary value");

      }

      return(r);
}

/*                                 
 * Vector Formatting.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, TAG Universal Machine.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */



const char b64[64] = {'A','B','C','D','E','F','G','H',
                     'I','J','K','L','M','N','O','P',
                     'Q','R','S','T','U','V','W','X',
                     'Y','Z','a','b','c','d','e','f',
                     'g','h','i','j','k','l','m','n',
                     'o','p','q','r','s','t','u','v',
                     'w','x','y','z','0','1','2','3',
                     '4','5','6','7','8','9','-','_'    // URL-friendly mod
                     };

const char bc[58] = {'A','B','C','D','E','F','G','H',
                     'J','K','L','M','N','P',
                     'Q','R','S','T','U','V','W','X',
                     'Y','Z','a','b','c','d','e','f',
                     'g','h','j','k','l','m','n',
                     'p','q','r','s','t','u','v',
                     'w','x','y','z','1','2','3',
                     '4','5','6','7','8','9'
                     };

const int b64_padding = 0;

char* b64triplet = NULL;

/* Convert 3 bytes into 4 base64 characters */
char* encode_b64_triplet(uint8_t b1, uint8_t b2, uint8_t b3){
  char* r = malloc(5);
  memset(r, '\0', 5);
  uint8_t idx1, idx2, idx3, idx4;
  
  idx1  = ((b1 & 0xFC) >> 2);                       // First 6 Bits of b1
  idx2  = ((b1 & 0x03) << 4) | ((b2 & 0xF0) >> 2);  // Last 2 bits of b1 and first 4 of b2
  idx3  = ((b2 & 0x0F) << 2) | ((b3 & 0xC0) >> 4);  // Second nibble of b2, first 2 bits of b3
  idx4  =  (b3 & 0x3F);                             // Last 6 bits of b3

  r[0] = b64[idx1];
  r[1] = b64[idx2];
  r[2] = b64[idx3];
  r[3] = b64[idx4];
  r[4] = '\0';
  return(r);
}

char cell_to_char(cell c) 
{
   char r;
   switch (c)
        {
            case CELL_FALSE : r = '0';
                     break;

            case CELL_TRUE  : r = '1';
                     break;

            case CELL_NULL  : r = 'N';
                     break;

            default : r = '?';
        }

  return (r);
}

/* Format Vector 
   Caller frees returned string
 */
char* fmt_vecbe(vec128bec_t* v, int fmt_type){
  int i;
  uint32_t int32;
  int offset;

  if (v == NULL) { return "[NULL]"; }

  char* r = malloc(128*10);
  memset(r, '\0', 128*10);

  char* t = malloc(10);
  memset(t, '\0', 10);
  
  switch (fmt_type) {
      /* BINARY */
      case FMT_VEC_BINARY :  
                 for (i = 1; i <= 16; i++){ 
                   sprintf(t,"%02X",vecbe_get_byte(i, v));
                   strcat(r,t);
                 }
                 break;

      /* SHA1 - Form only, not SHA1 function */
      case FMT_VEC_SHA1 : 
               offset = 0;
               for (i=0; i<16; i++) {

                 if ((i==4) || (i==10)) {
                    sprintf(&r[(i*2)+offset],"-%02x",vecbe_get_byte(i+1, v));
                    offset++;
                    continue;
                 }

                 if (i==6) {
                    sprintf(&r[(i*2)+offset],"-%02x",(vecbe_get_byte(i+1, v) & 0x0F) | (0x03 << 4)); // Left nibble to 0x3
                    offset++;
                    continue;
                 }

                if (i==8) {
                    sprintf(&r[(i*2)+offset],"-%02x",(vecbe_get_byte(i+1, v) & 0x0F) | (0x0A << 4)); // Left nibble to 0xA
                    offset++;
                    continue;
                 }

                 sprintf(&r[(i*2)+offset],"%02x",vecbe_get_byte(i+1, v));
               }
               break;

      /* Text BINARY */
      case FMT_VEC_BINARY_TEXT :   
                  for (i = 1; i <= 128; i++){   
                    sprintf(t,"%c", cell_to_char(get_cell(v, (129-i))));
                    strcat(r,t);
                  }

              break;

      case 3:   sprintf(r," ");
                    break;

      /* IPV4 */
      case FMT_VEC_IPV4:   
                 sprintf(r,"%d.%d.%d.%d",(vecbe_get_byte(4,v)),(vecbe_get_byte(3,v)),(vecbe_get_byte(2,v)),(vecbe_get_byte(1,v)));
                 break;


      /* GUID 
         One to three of the most significant bits of the first byte in Data 4 define the type variant of the GUID
         Set to 100 or 0x04
       */
      case FMT_VEC_GUID: 
               offset = 1;
               for (i=0; i<16; i++) {

                 if ((i==4) || (i==10)) {
                    sprintf(&r[(i*2)+offset],"-%02X",vecbe_get_byte(i+1, v));
                    offset++;
                    continue;
                  }

                if (i==8) {
                    sprintf(&r[(i*2)+offset],"-%02X",(vecbe_get_byte(i+1, v) & 0x0F) | (0x0A << 4)); // Left nibble to 0x4
                    offset++;
                    continue;
                 }

                 sprintf(&r[(i*2)+offset],"%02X",vecbe_get_byte(i+1, v));
               }
               
               r[0] = '{';  strcat(r,"}");
               break;

      /* IPV6 */
      case FMT_VEC_IPV6: 
              offset = 0;
              for (i=0; i<16; i++) {
                  if ((i % 2 == 0) && (i > 0)) {
                    sprintf(&r[(i*2)+offset],":%02x",vecbe_get_byte(i+1, v));
                    offset++;
                  } else {
                    sprintf(&r[(i*2)+offset],"%02x",vecbe_get_byte(i+1, v));
                  }
              }
              break;

      case 7:   sprintf(r," ");
                break;

      /* PSI Fingerprint 
         Technically this is a complete NDRAND, not just a fingerprint, but for 
         code simplicity and because there is no functional difference in the von Neumann 
         architecture, it is generated the same way as the others.
      */
      case FMT_VEC_PSI :  
                 strcat(r,"[<:");
                 for (i=0; i<16; i++) {
                   sprintf(&r[(i*2)+3],"%02X",(vecbe_get_byte(16-i,v)));
                  }
                 strcat(r,":>]");
                 break;

      case 9:   sprintf(r," ");
                break;

      /* Unsigned 32-bit */           
      case FMT_VEC_INT32 :  
                 int32 = ((vecbe_get_byte(4,v)) << 24) | ((vecbe_get_byte(3,v)) << 16) | ((vecbe_get_byte(2,v)) << 8) | ((vecbe_get_byte(1,v)));
                 sprintf(r, "%" PRIu32, int32);
                 break;

             /* UUID V4 */
      case FMT_VEC_UUID : 
               offset = 0;
               for (i=0; i<16; i++) {

                 if ((i==4) || (i==10)) {
                    sprintf(&r[(i*2)+offset],"-%02x",vecbe_get_byte(i+1, v));
                    offset++;
                    continue;
                  }
                 
                 if (i==6) {
                    sprintf(&r[(i*2)+offset],"-%02x",(vecbe_get_byte(i+1, v) & 0x0F) | (0x04 << 4)); // Left nibble to 0x4
                    offset++;
                    continue;
                 }

                if (i==8) {
                    sprintf(&r[(i*2)+offset],"-%02x",(vecbe_get_byte(i+1, v) & 0x0F) | (0x0A << 4)); // Left nibble to 0xA
                    offset++;
                    continue;
                 }

                 sprintf(&r[(i*2)+offset],"%02x",vecbe_get_byte(i+1, v));
               }
               break;


      /* Base 64 - Produces 22 characters */
      case FMT_VEC_BASE64:   // Encode first 15 bytes in 5 pairs of 3
                 // 1,2,3  4,5,6  7,8,9  10,11,12  13,14,15
                 for(i=1; i<=5; i++){
                   offset = ((i-1) * 3) + 1;
                   b64triplet = encode_b64_triplet(vecbe_get_byte(offset, v),
                                                   vecbe_get_byte(offset+1, v),
                                                   vecbe_get_byte(offset+2, v));
                   sprintf(t, "%s", b64triplet);                                                                                                          
                   strcat(r,t);

                   free(b64triplet);
                 }
                 // Remaining byte 16
                 b64triplet = encode_b64_triplet(vecbe_get_byte(16, v), 0,0);
                 sprintf(t, "%s", b64triplet);
                 // Keep first two hex64 digits and the rest is padding (==) if desired.
                 if (b64_padding) {
                   t[2] = '=';
                   t[3] = '=';
                   t[4] = '\0';
                 } else {
                   t[2] = '\0';
                 }
                 strcat(r,t);
                 free(b64triplet);
                 break;

       case 13:   sprintf(r," ");
                       break;

       default:  sprintf(r,"INVALID FORMAT\n");       
      
  }

  free(t);
  return(r);
}


char* frame_to_str(frame_t* f, int fmt_type) {
  int i;
  vec128bec_t* v = vec_alloc();

  char* r = malloc(2000);
  memset(r, '\0',2000);

  char* s = malloc(500);
  memset(s, '\0',500);

  if (f->count == 0) {
    sprintf(r,"[STACK EMPTY]\n");
  }

  for (i=f->count; i >= 1; i--){
    frame_get_row(f, i, v);
    sprintf(s, "[%03d]     %s\n",i,fmt_vecbe(v, fmt_type));
    strcat (r,s);
    memset(s, '\0',500);
  }

  free(s);
  free(v);

  return(r);
}

/*                                 
 * Machine Logic
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, TAG Universal Machine.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

 /* Random means YOU do the choosing.
 *  -- Ross Ashby
 *
 *
 *                                        
 *                                                        Cellular Automata Rule 30                                    Universal Constant
 *                                                                 Field of Action                                                           R30
 */




// Check the nth bit from the right end
#define CHECK_BIT(var,pos) ((var) & (1<<(pos)))
// CHECK_BIT(temp, n - 1)

/* Time Seed
 * A catalyst. 96 bits of environmental entropy in the form of
 * internal (Relative, Cyclic) and external (Absolute, Passing) time. 
 * Entropy is amplified to 128 bits to create the PSI fingerprint.
 * ts_cycle is the internal cycle counter to guarantee differentiation from call to call,
 * even if RTC clock does not increment.
 */

static int initialized = 0;  // Track if cp_init() has been called
static uint32_t ts_cycle = 0;

static cell_proc_t *cp = NULL;  // Global pointer


TimeSeed time_seed(void)
{
  struct timeval tv; 
  ts_cycle += 1;
  if (gettimeofday(&tv, NULL) != 0) { halt("TimeSeed: gettimeofday failed");}               

  TimeSeed r = {.long_count = tv.tv_sec,    // RTC seconds
                .short_count = tv.tv_usec,  // RTC microseconds
                .cyclic = ts_cycle};        // Internal cyclic state

  return (r);
}

void initialize_mkrand(void) {
  if (!initialized) {
    if (cp == NULL) {
        cp_init();  // Initialize processor
    }
  }
}

char* timeseed_tostr(TimeSeed* ts) {
   size_t  strsize = 100;
   char* r = malloc(strsize);
   memset(r, '\0', strsize);
   sprintf(r, "LC:%08" PRIX32 " SC:%08" PRIX32 " CY:%08" PRIX32, 
           ts->long_count, ts->short_count, ts->cyclic);
   return (r);
}


vec128bec_t* time_seed_to_vec(TimeSeed seed) {
   vec128bec_t* ts = vec_alloc();
   /* Convert 
      long count : long int (32 bits)
      short count: long int (32 bits)
      cyclic : long int     (32 bits)
      96 bits on the vector
   */

   int index = 1;
   // Long count
   for(int i=0; i<32; i++) {
     set_cell(ts, index + i, ((seed.long_count >> i) & 0x01) ? CELL_TRUE : CELL_FALSE);
   }

   // short count
   for(int i=0; i<32; i++){
    set_cell(ts, index + i + 32, ((seed.short_count >>i) & 0x01) ? CELL_TRUE : CELL_FALSE);
   }

   // cyclic
   for(int i=0; i<32; i++){
    set_cell(ts, index + i + 64, ((seed.cyclic >> 1) & 0x01) ? CELL_TRUE : CELL_FALSE);
   }

   // set remaining bits to CELL_FALSE
   for(int rest=96; rest<=128; rest++){
    set_cell(ts, rest, CELL_FALSE);
   }

   // every bit should be either CELL_TRUE or CELL_FALSE

   return (ts);
}

vec128bec_t* hash_seed_to_vec(const uint8_t hash[16]) {
    vec128bec_t* vec = vec_alloc();
    if (!vec) return NULL;

    // Populate vec using set_cell() (1-based indexing)
    for (int i = 0; i < 16; i++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t bit_value = (hash[i] >> bit) & 0x01;
            set_cell(vec, (i * 8) + bit + 1, bit_value ? CELL_TRUE : CELL_FALSE);
        }
    }

    return vec;
}



/* 
   Convert a string in PSI format [<:838087396B4405BCF017731EF1F99653:>] to vector
*/
int seed_str_to_vec(const char* seed_str, vec128bec_t* out_vec) {
  int r = 0;
  char byte_string[3];
 /* printf("SEED_STR_TO_VEC\n"); */
 // char* register_str;
 // register_str = fmt_vecbe(out_vec, FMT_VEC_PSI);
 /* printf("%s\n", register_str); */
  if (seed_str == NULL) {
    vsetN(out_vec);
    return(-1);
  }

  if ((strlen(seed_str) == 38) && (seed_str[0] == '[') && (seed_str[1] == '<')  && (seed_str[2] == ':') &&
     (seed_str[35] == ':') && (seed_str[36] == '>')  && (seed_str[37] == ']'))
  {
    /* printf("SEED_STR_TO_VEC Looks Ok, converting bytes\n"); */
    for (int byte_num=0; byte_num < 16; byte_num++) {
      sprintf(byte_string, "%c%c",seed_str[(byte_num*2)+3], seed_str[((byte_num*2)+1)+3]);
   /*   printf("Byte:(%d):%s\n", byte_num, byte_string);  */
      int byte_int = (int)strtol(byte_string, NULL, 16);
  /*   printf("Converted to %d\n", byte_int);  */
      for (int bit=7; bit>=0; bit--){
        cell cell_val = CELL_FALSE;
        
        if (CHECK_BIT(byte_int, bit)) {
       /*   printf("CHECK_BIT %d,%d TRUE\n",byte_int, bit); */
          cell_val = CELL_TRUE;
        } else {
       /*   printf("CHECK_BIT %d,%d FALSE\n",byte_int, bit); */
          cell_val = CELL_FALSE;
        }
        int cell_num = 129 - ((byte_num * 8) + (7-bit) + 1);
     /*   printf("Setting cell %d to %d\n", cell_num, cell_val);  */
        set_cell(out_vec, cell_num, cell_val);
      }
    }
    return(0);
  } else {
    LOG_ERROR("SEED_STR_TO_VEC FORMAT CHECK FAILED, seed length was %zu\n", strlen(seed_str));
    vsetN(out_vec);
    return(-1);
  }
  return(r);
}



/* Compare TimeSeeds, return CELL_TRUE if equal, CELL_FALSE otherwise */
int ts_cmpE(TimeSeed* s0,  TimeSeed* s1){
  return ((s0->long_count  == s1->long_count)  &&
          (s0->short_count == s1->short_count) &&
          (s0->cyclic      == s1->cyclic)) ? CELL_TRUE : CELL_FALSE; 
}

void cp_setstate(struct cell_proc_t *restrict cp, cp_state_t st){
  cp->s = st;
}

cp_state_t cp_getstate(struct cell_proc_t *restrict cp) {
  return (cp->s);
}

void cp_halt(struct cell_proc_t *restrict cp, char* msg){
  cp_setstate(cp, CP_HALT);
  LOG_ERROR("Program Halt - %s.\n", msg);
  exit(EXIT_FAILURE);
}

/* Nullify memory slots and reset state
   C - does not deallocate any memory
 */
void cp_reset(struct cell_proc_t *restrict cp){
  cp->index = 0;
  cp->counter_mode = 0;
  cp->NR = cp->B;
  cp->CR = cp->A;

  cp_setstate(cp, CP_IDLE);
  frame_clear(cp->Stack);
  vsetN(cp->A);
  vsetN(cp->B);
  vsetN(cp->C);
  vsetN(cp->D);
  vsetN(cp->PSI);
  vsetN(cp->R30);
  vsetN(cp->R);
  vsetZ(cp->SDR30);
  vsetN(cp->SDTIME);
}

uint16_t cp_init(void) {
    if (cp == NULL) {
        cp = (cell_proc_t *)malloc(sizeof(cell_proc_t));
        if (!cp) {
            LOG_ERROR("❌ cp_init: Memory allocation failed!\n");
            exit(1);
        }
        LOG_INFO("✅ cp allocated at %p\n", (void *)cp);
    }

    cp->A      = vec_alloc();
    cp->B      = vec_alloc();
    cp->C      = vec_alloc();
    cp->D      = vec_alloc();
    cp->PSI    = vec_alloc();
    cp->R30    = vec_alloc();
    cp->SDR30  = vec_alloc();
    cp->SDTIME = vec_alloc();
    cp->R      = vec_alloc();
    cp->Stack  = frame_alloc();
    cp_setstate(cp, CP_NULL);

    if (!((cp->A) && (cp->B) && (cp->C) && (cp->PSI) && (cp->R30) && (cp->R))) {
        LOG_ERROR("❌ cp_init: Out of Memory!\n");
        exit(1);
    }

    cp_reset(cp);
    LOG_INFO("✅ cp initialized successfully at %p\n", (void *)cp);
    return 0;
}



/* Deallocate memory slots */
void cp_free(struct cell_proc_t *restrict cp){
  free(cp->A);
  free(cp->B);  
  free(cp->C);   
  free(cp->D);  
//  free(cp->X);
  free(cp->PSI); 
  free(cp->R30); 
  free(cp->SDR30); 
  free(cp->SDTIME); 
  free(cp->R);   
  frame_free(cp->Stack);
}

/* Machine Instructions */

/* CMPZ - Return CELL_TRUE if all cells are False, CELL_FALSE otherwise */
cell_state_t mi0_cmpZ (vec128bec_t* v) {
  uint8_t i;
  cell c;
  cell_state_t r = CELL_NULL;

  for(i = 1; i <= 128; i++) {
     c = get_cell(v, i);
     if ((c == CELL_TRUE) || (c == CELL_NULL)) {
        return (CELL_FALSE);
     }
  }

  r = CELL_TRUE;

  return r;
}

/* CMPN - Return CELL_TRUE if ANY cell is NULL, CELL_FALSE otherwise */
cell_state_t mi0_cmpN (vec128bec_t* v) {
  uint8_t i;

  for(i = 1; i <= 128; i++) {
     if (get_cell(v, i) == CELL_NULL) {
        return (CELL_TRUE);
     }
  }

  return (CELL_FALSE);
}


/* XOR A and B, place result in D     */
void mi0_xor  (cell_proc_t * restrict cp) {
  int i;

  if ((mi0_cmpN(cp->A) == CELL_TRUE) || (mi0_cmpN(cp->B) == CELL_TRUE)) { cp_halt(cp, "MI_XOR: NULL Argument(s)");}
  vsetN(cp->D);

  for (i = 1; i <= 128; i++) {
    set_cell(cp->D, i, cxor(get_cell(cp->A, i), get_cell(cp->B,i)));
  }

}


/* LDA - Load vector into register A */
void mi_LDA (cell_proc_t *restrict cp, vec128bec_t* input) {
  vcopy(input, cp->A);
}

/* LDB - Load vector into register B */
void mi_LDB (cell_proc_t *restrict cp, vec128bec_t* input) {
  vcopy(input, cp->B);
}

/* LDC - Load vector into register C */
void mi_LDC (cell_proc_t *restrict cp, vec128bec_t* input) {
  vcopy(input, cp->C);
}

/* LDD - Load vector into register D */
void mi_LDD (cell_proc_t *restrict cp, vec128bec_t* input) {
  vcopy(input, cp->D);
}

/* Rule 30 : x(n+1,i) = x(n,i-1) xor [x(n,i) or x(n,i+1)] */
cell rule_30(cell left, cell middle, cell right){
  return (cxor (left, cor (middle, right)));
}


/* Evaluate source vector with elemental rule, placing result in dest  */
void eval_rule(rule_t r, vec128bec_t* source, vec128bec_t* dest){
  uint8_t col;
  cell left_cell, right_cell, middle_cell;

  for (col = 1; col <= 128; col++){
     left_cell   = (col == 128) ? get_cell(source, 1)    : get_cell(source, col+1);
     right_cell  = (col == 1)   ? get_cell(source, 128)  : get_cell(source, col-1); 
     middle_cell = get_cell(source, col);
            
     set_cell(dest, col, rule_30(left_cell, middle_cell, right_cell) );   
  }  
}

/* Update Time Seed with current time   
   96 bits of clock information are loaded such that long count is in MSB, 
      short count on LSB with and cyclic in center:
   Short Count (32) -> Random(16) -> Cyclic (32) <- Random(16) <- Long Count (32)
   The rest of the bits are seeded with randomness from previous cycle, or 0 on init
 */ 
void mi0_incSDTIME (cell_proc_t *restrict cp){
  int i;
  char* out_str = NULL;
  TimeSeed seed = time_seed();
  
  if (verbose_flag) {  
    LOG_INFO("MI2_INCSDTIME: %s\n",timeseed_tostr(&seed));
  }   

  if (mi0_cmpN(cp->R) == CELL_TRUE) {
    vsetZ(cp->SDTIME);         // On init, pre-Seed with 0
  } else {
    vcopy(cp->R, cp->SDTIME);  // pre-Seed with previous output for cycle mode
  }                            
  
  /* MSB 1-32 */
  for (i = 1; i <= 32; i++){
    set_cell(cp->SDTIME, i, binary_to_cell ((seed.long_count  >> (i-1)) & 0x01));  
  }

  /* Center 48-80 */
  for (i=1; i<=32; i++){
     set_cell(cp->SDTIME, (48+i), binary_to_cell ((seed.cyclic >> (i-1)) & 0x01));  
  }

  /* LSB 96-128 */
  for (i = 1; i <= 32; i++){
    set_cell(cp->SDTIME, (96+i), binary_to_cell ((seed.short_count >> (i-1)) & 0x01));  

  }  

  if (verbose_flag) {  
    out_str = fmt_vecbe(cp->SDTIME, FMT_VEC_BINARY);
    LOG_INFO("SDTIME: %s\n",out_str);
    free(out_str);
  }  

}

/* INCPSI - SHA30 of SDTIME, place result in PSI */
void mi2_incPSI (cell_proc_t *restrict cp){

  if (verbose_flag) {  
    LOG_INFO("MI2_INCPSI\n");
  }   

  vsetN(cp->PSI);
  if (cp->counter_mode == 1) {
     vcopy(cp->SDR30, cp->A);
  } else {
     vcopy(cp->SDTIME, cp->A);
  }
 
  mi2_sha30(cp);
  vmov(cp->D, cp->PSI);
}

/* SHA30 - Use REGA as seed for R30, generate 2 blocks, move second block to REGD
 */
void mi2_sha30 (cell_proc_t *restrict cp){

  if (verbose_flag) {  
    LOG_INFO("MI2_SHA30\n");
  }   
 /* pushSD(cp); */
  vsetN(cp->D);
  vmov(cp->A, cp->SDR30);
  mi1_incR30(cp);
  mi1_incR30(cp);
  if (cp->counter_mode) {
    vcopy(cp->SDR30, cp->D);
  } else {
    vmov(cp->SDR30, cp->D);
  }
  
 /* popSD(cp);  // Restore Seeds */
}

/* Generate non-deterministic rand, PSI must already exist 
   Take SHA30 of PSI, then XOR that to PSI. Move result to R
 */

void mi2_genR (cell_proc_t *restrict cp) {
  if (verbose_flag) {  
    LOG_INFO("MI2_GENR\n");
  }   
  vsetN (cp->R);
  vcopy(cp->PSI, cp->A);    // PSI -> A
  mi2_sha30(cp);            // CELEST(PSI) -> D
  vcopy(cp->PSI, cp->A);    // PSI -> A
  vcopy(cp->D, cp->B);      // A:PSI B: CELEST(PSI)
  mi0_xor(cp);              // D: RAND
  vmov(cp->D, cp->R);       // R: PSI XOR CELEST(PSI)
}


/* Machine instruction R30
   Runs R30 for 128 clock cycles to fill R30 with next segment
   RegA : Seed, copied from SDR30, will set center column to 1 if R30 is zero
   RegB : Buffer
   RegD : Output 
   Copies last state to SDR30
 */
void mi1_incR30 (cell_proc_t *restrict cp){
  uint32_t gen;
  uint32_t center_col = 0;
  char* out_str = NULL;

  if (verbose_flag) {  
    LOG_INFO("MI1_INCR30\n");
  }   

  if (cp_getstate(cp) != CP_IDLE)       { cp_halt (cp, "INCR30: Celproc not idle");  }

  if (mi0_cmpN(cp->SDR30) == CELL_TRUE) { cp_halt (cp, "INCR30: Null Input on SDR30"); }

  vmov(cp->SDR30, cp->A);    // Copy R30 Seed to input 

  vsetN(cp->B);              // Clear Buffer

  cp->NR = cp->A;            // Set row pointers
  cp->CR = cp->B;

  cp_setstate(cp, CP_RUN);

  center_col = (128 / 2);

  if (mi0_cmpZ(cp->A) == CELL_TRUE) {
    set_cell(cp->A, center_col, CELL_TRUE);
  }

  for (gen = 1; gen <= 128; gen++) {
    if (gen & 0x01) {    // Toggle Row pointers every other gen
      cp->NR = cp->B;
      cp->CR = cp->A;
    } else {
      cp->NR = cp->A;
      cp->CR = cp->B;
    }

    if (verbose_flag) {
      out_str = fmt_vecbe(cp->CR, FMT_VEC_BINARY_TEXT);
      LOG_INFO("[%" PRIu32 "]     %s %c\n", gen, out_str, cell_to_char(get_cell(cp->CR, center_col)));
      free(out_str);
    }
    
    eval_rule(30, cp->CR, cp->NR);
    
    set_cell(cp->D, gen, get_cell(cp->NR, center_col)); //  Copy center cell to D[gen]
  } 

  vmov(cp->CR, cp->SDR30);    /* Update Seed        */
  vmov(cp->D,  cp->R30);      /* Move result to R30 */

  cp_setstate(cp, CP_IDLE);  
}

/* During Time Quantum :
   I/O input transfers occur (A,B loaded), get_neighbor calls resolved 
   TimeSeed (SDTIME) is updated
   R30 is atomically advanced by 128 bits
   PSI is atomically updated
   R is atomically updated based on PSI
   1 Process Cycle occurs
   I/O output transfers occur (D loaded)
   Can be reduced by 1 quantum when incR30 is moved to previous cycle
 */
void mi5_time_quantum(cell_proc_t* restrict cp){
  if (verbose_flag) { LOG_INFO ("Time Quantum\n");}

  if (!(cp_getstate(cp) == CP_IDLE)) { cp_halt(cp, "time_quantum : CellProc not Idle"); }
  /* Increment time seed if not in counter mode */
  if (cp->counter_mode == 1) {
    if (verbose_flag) { LOG_INFO ("Time Quantum - counter mode");}
  /* printf("mi5_time_quantum SDR30:\n");
    print_vec(cp->SDR30); */
  } else {
    if (verbose_flag) { LOG_INFO ("Time Quantum - incSDTIME\n");}
    mi0_incSDTIME(cp);
  }
  
  if (verbose_flag) { LOG_INFO ("Time Quantum - incR30\n");}
  mi1_incR30(cp);  /* SDR30 -> SDR30 */
  vcopy(cp->SDR30, cp->R);
  if (verbose_flag) { LOG_INFO ("Time Quantum - incPSI\n");}
  /* mi2_incPSI(cp); */
  if (verbose_flag) { LOG_INFO ("Time Quantum - genR\n");}
 /* mi2_genR(cp); */
  if (verbose_flag) { LOG_INFO ("Time Quantum - end\n");}
}

/* Push Seed Registers onto stack */
void pushSD(cell_proc_t* restrict cp) {
  frame_push(cp->SDTIME, cp->Stack);
  frame_push(cp->SDR30,  cp->Stack);
  if (verbose_flag) { 
     LOG_INFO("\nPUSHSD\n%s\n", frame_to_str(cp->Stack, FMT_VEC_BINARY_TEXT)); 
  }  
}

/* Pop Seed Registers from stack */
void popSD(cell_proc_t* restrict cp){
  if (verbose_flag) { 
     LOG_INFO("\nPOPSD\n%s\n", frame_to_str(cp->Stack, FMT_VEC_BINARY_TEXT)); 
  }   
  frame_pop(cp->Stack, cp->SDR30);
  frame_pop(cp->Stack, cp->SDTIME);  
}

/* Push General Purpose Registers onto stack */
void pushGP(cell_proc_t* restrict cp) {
  frame_push(cp->A, cp->Stack);
  frame_push(cp->B, cp->Stack);
  frame_push(cp->C, cp->Stack);
  frame_push(cp->D, cp->Stack);

  if (verbose_flag) { 
     LOG_INFO("PUSHGP\n%s\n", frame_to_str(cp->Stack, FMT_VEC_BINARY_TEXT)); 
  }  
}

/* Pop General Purpose Registers from stack */
void popGP(cell_proc_t* restrict cp) {
  frame_pop(cp->Stack, cp->D);
  frame_pop(cp->Stack, cp->C);
  frame_pop(cp->Stack, cp->B);
  frame_pop(cp->Stack, cp->A);

  if (verbose_flag) { 
     LOG_INFO("POPGP\n%s\n", frame_to_str(cp->Stack, FMT_VEC_BINARY_TEXT)); 
  }  
}

/* Check clocks
 * Return 0 if OK, print error and halts otherwise
 */
int check_clocks(void)
{  
  int r = 0;

  if (verbose_flag) { LOG_INFO("Clock Check\n"); }
   
  /* Test Clock */

  TimeSeed s0 = time_seed();
  TimeSeed s1 = time_seed();

  if (verbose_flag) {
    LOG_INFO("s0: %s\n",timeseed_tostr(&s0));
    LOG_INFO("s1: %s\n",timeseed_tostr(&s1));
  }

  /* Error if TimeSeeds did not change */
  if (ts_cmpE(&s0, &s1) == CELL_TRUE) {  
     halt("Clock failed\n");
  }

  return (r);
}



/*                                 
 * Command-line processing.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, TAG Universal Machine.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */




typedef struct usage_t {
  char* name;
  char* format;
  char* description;
} usage_t;

typedef struct fib_t {
  size_t n1;
  size_t n2;
} fib_t;

typedef struct profile_t {
  size_t rbps;            // Randomness in bps
  float entropy;          // H
} profile_t;





unsigned int uint_limit( unsigned int lower, unsigned int upper, unsigned int val){
    return ((val < lower) ? lower : (val > upper ? upper : val));
}

fib_t* new_fib() {
  fib_t* t = malloc(sizeof(fib_t));
  t->n1 = 1;
  t->n2 = 2;
  return (t);
}

size_t fib (fib_t* f) {
  size_t r = f->n1 + f->n2;
  f->n1 = f->n2;
  f->n2 = r;
  return (r);
}


void to_stream(FILE* stream, size_t num_blocks, int output_format, vec128bec_t* v){
size_t i;
char* out_str;
uint8_t* bytes;
  
  if ((output_format == FMT_VEC_BINARY) && (stream != stdout)) {    /* Binary File */
        bytes = malloc(16 * num_blocks);
        if (bytes == NULL) { halt  ("to_stream : Out of Memory"); }
        
        for (i=0; i<num_blocks; i++) {
           vecbe_pack(&bytes[i*16],&v[i]);
        }     
        fwrite(bytes, 16, num_blocks, stream);
        free(bytes);
  } else {                                                          /* Text File */
      for (i=0; i<num_blocks; i++) {
        out_str = fmt_vecbe(&v[i], output_format);
        fprintf(stream,"%s\n",out_str);
        free(out_str);
        } 
  }
}



/* Profile
 * Run escalating loads until desired number of seconds have elapsed.
 * Calculates bps produced
 */
profile_t profile(cell_proc_t* cp, unsigned int profile_seconds){
  profile_t r;
  size_t i,j;
  size_t load = 0;
  size_t cumulative_load = 0;

  fib_t* f = new_fib();
  unsigned int start_time = (unsigned int) time (NULL);
  unsigned int current_time;
  unsigned int cumulative_seconds = 0;
  float bps = 0;
  char speed_factor = ' '; // k M 
  
  /* Profile rand */
  for (i=0; i<100; i++) {
     load = fib(f);
     cumulative_load += load;  // Number of vectors produced
     for (j=0; j<load; j++){
       mi5_time_quantum(cp);   
     }
     current_time = (unsigned int) time (NULL);
     cumulative_seconds += current_time - start_time;

     if (cumulative_seconds >= profile_seconds) {
        bps =  ((cumulative_load * 128) / cumulative_seconds);
        r.rbps = bps;

        if (bps > 1000000) {
          speed_factor = 'M';
          bps = bps / 1000000;
        } else if (bps > 1000) {
              speed_factor = 'k';
              bps = bps / 1000;
        }  

        break;
     }
  }

  /* Entropy measurement 
      Calculate H per Shannon's Mathematical Theory of Communication
   */
  uint32_t weight = 0;
  uint32_t hblocks = 1000; 
  double_t p_0 = 0;
  double_t p_1 = 0;

  for (i=0; i<hblocks; i++) {
     mi5_time_quantum(cp);
     weight += vecbe_hamming_weight(cp->R);
  }

  p_1 = ((double_t) weight) / (hblocks * 128);     // Probability of Bit = 1
  p_0 = 1.0 - p_1;                                               // Probability of Bit = 0
  
  r.entropy =  0.0 - ((p_1 * (log2f(p_1))) + (p_0 * (log2f(p_0))));

  printf("%0.2f %cbps  Entropy (H): %0.5f\n", bps,  speed_factor, r.entropy);
  free(f);
  return (r);
}



void usage(cell_proc_t* cp){
  char* out_str = NULL;
  static usage_t u[14];
  int i;

  vec128bec_t* v = vec_alloc();

  for (i=0; i<NUM_FORMATS; i++){
    mi5_time_quantum(cp);
    vmov(cp->R,v);

    switch (i) {
      case FMT_VEC_BINARY: 
              u[i].name = "Pure";
              u[i].description = "128-bit Binary";
              u[i].format = fmt_vecbe(v, i);
              break;

      case FMT_VEC_SHA1: 
              u[i].name = "SHA1";
              u[i].description = "SHA1 Format";
              u[i].format = fmt_vecbe(v, i);
              break;

      case FMT_VEC_BINARY_TEXT: 
              u[i].name = "BINARY";
              u[i].description = "Text Mode Binary";
              u[i].format = malloc(128+1);
              memset(u[i].format, '\0', 128+1);
              strcat(u[i].format,"...");
              out_str = fmt_vecbe(v, i);
              strncpy(u[i].format+3,out_str,30);
              u[i].format[32] = '\0';        
              free(out_str);
              break;

      case 3: u[i].name = "RESERVED";
              u[i].description = "";
              u[i].format = fmt_vecbe(v, i); 
              break;

      case FMT_VEC_IPV4: 
               u[i].name = "IPV4";
               u[i].description = "IPV4 Address";
               u[i].format = fmt_vecbe(v, i);
               break;

      case FMT_VEC_GUID: 
              u[i].name = "GUID";
              u[i].description = "Globally Unique ID";
              u[i].format = fmt_vecbe(v, i); 
              break;

      case FMT_VEC_IPV6: 
              u[i].name = "IPV6";
              u[i].description = "IPV6 Address";
              u[i].format = fmt_vecbe(v,i);
              break;

      case 7: u[i].name = "RESERVED";
              u[i].description = "";
              u[i].format = fmt_vecbe(v, i); 
              break;

      case FMT_VEC_PSI: 
              u[i].name = "PSI";
              u[i].description = "Time Fingerprint";
              u[i].format = fmt_vecbe(v,i);
              break;

      case 9: u[i].name = "RESERVED";
              u[i].description = "";
              u[i].format = fmt_vecbe(v, i); 
              break;

      case FMT_VEC_INT32: 
               u[i].name = "INT";
               u[i].description = "32-bit Unsigned Integer";
               u[i].format = fmt_vecbe(v,i);
               break;

      case FMT_VEC_UUID: 
              u[i].name = "UUID V4";
              u[i].description = "Universally Unique ID";
              u[i].format = fmt_vecbe(v, i); 
              break;

      case FMT_VEC_BASE64: 
              u[i].name = "BASE64";
              u[i].description = "Text Encoding";
              u[i].format = fmt_vecbe(v, i); 
              break;

      case 13: u[i].name = "RESERVED";
              u[i].description = "";
              u[i].format = fmt_vecbe(v, i); 
              break;

      default : u[i].name = "UNDEFINED";
                u[i].description = "UNDEFINED";
                u[i].format = "UNDEFINED";

    } 
  }   

  char* msg = malloc(10000);
  if (msg == NULL) { halt("Out of Memory");}
  memset(msg, '\0', 10000);
  
  strcat(msg, "MKRAND - A Digital Random Bit Generator (DEBUG)\n");
  strcat(msg, "Copyright (c) 2013 TAG Universal Machine.\n\n");
  strcat(msg, "USAGE: mkrand [-s seed] [-f format] [-n blocks] [-o filename] [--profile] [--verbose]\n");
  strcat(msg, "Formats:\n");

  printf("%s",msg);

  for (i=0; i<NUM_FORMATS; i++){
       printf("%2d  - %10s      %40s     %30s\n",i,u[i].name, u[i].format, u[i].description);
       free(u[i].format);
     }  
  free(v);
  free(msg);
}

int print_vec(vec128bec_t* vec) {
  char* out_str = fmt_vecbe(vec, FMT_VEC_PSI);
  printf("%s\n", out_str);
  free(out_str);
  return(0);
}


int generate_block(const char *seed, char *block_out) {
    initialize_mkrand();  // Ensure MKRAND is ready

    vec128bec_t seed_vec;

    if (seed == NULL) {
        // Generate fresh (nondeterministic) block
        vmov(time_seed_to_vec(time_seed()), cp->SDR30);
        mi5_time_quantum(cp); 
    } else {
        // Generate next block deterministically from seed
        if (seed_str_to_vec(seed, &seed_vec) != 0) {
            return -1;  // ❌ Invalid seed format
        }
        vmov(&seed_vec, cp->SDR30);
        mi5_time_quantum(cp);  // Advance state for deterministic next block
    }

    // Convert result to text format
    strncpy(block_out, fmt_vecbe(cp->SDR30, FMT_VEC_PSI), 39);
    return 0;  // ✅ Success
}


char* new_block(void) {
    LOG_INFO("🔹 MKRAND new block generation...\n");

    // ✅ Ensure `cp` is initialized
    if (!cp) {
        LOG_WARN("⚠️ Warning: cp is NULL. Initializing with cp_init()...\n");
        cp_init();
    }

    vec128bec_t* seed_vec = vec_alloc();
    if (!seed_vec) {
        LOG_ERROR("❌ Error: vec_alloc() returned NULL!\n");
        return NULL;
    }
    LOG_INFO("✅ Allocated memory for seed vector: %p\n", (void *)seed_vec);

    seed_vec = time_seed_to_vec(time_seed());
    if (!seed_vec) {
        LOG_ERROR("❌ Error: time_seed_to_vec() returned NULL!\n");
        free(seed_vec);
        return NULL;
    }
    LOG_INFO("✅ Time-seeded vector generated: %p\n", (void *) seed_vec);

    // ✅ Check cp->SDR30 after initialization
    if (!cp) {
        LOG_ERROR("❌ Error: cp is still NULL after cp_init()!\n");
        return NULL;
    }

    LOG_INFO("✅ Moving vector into MKRAND processor state...\n");
    vmov(seed_vec, cp->SDR30 );
    mi5_time_quantum(cp);

    char *block_str = strdup(fmt_vecbe(cp->SDR30, FMT_VEC_PSI));
    if (!block_str) {
        LOG_ERROR("❌ Error: strdup() failed!\n");
        free(seed_vec);
        return NULL;
    }

    LOG_INFO("✅ Successfully generated block: %s\n", block_str);

    free(seed_vec);
    return block_str;
}

int next_block(const char *seed, char *next_block, size_t buf_size) {
    initialize_mkrand();  // Ensure `cp` is initialized

    vec128bec_t seed_vec;  // Local storage for vector
    if (seed_str_to_vec(seed, &seed_vec) == 0) {  // Convert text to vector
        vmov(&seed_vec, cp->SDR30);  // Load into processor state
        mi5_time_quantum(cp);
        // Call fmt_vecbe() and check memory handling
        char *formatted_block = fmt_vecbe(cp->SDR30, FMT_VEC_PSI);
        if (!formatted_block) {
            return -1;  // fmt_vecbe failed
        }

        // Copy result into caller's buffer
        strncpy(next_block, formatted_block, buf_size - 1);
        next_block[buf_size - 1] = '\0';  // Ensure null-termination

        return 0;  // Success
    }
    return -1;  // Failure
}

void mkrand_generate_ipv6(const uint8_t* hash_seed, uint8_t out[16]) {
    LOG_INFO("🔹 MKRAND new block generation...\n");

    // ✅ Ensure `cp` is initialized
    if (!cp) {
        LOG_WARN("⚠️ Warning: cp is NULL. Initializing with cp_init()...\n");
        cp_init();
    }

    vec128bec_t* seed_vec = vec_alloc();
    if (!seed_vec) {
        printf("❌ Error: vec_alloc() returned NULL!\n");
        return;
    }
    LOG_INFO("✅ Allocated memory for seed vector: %p\n", (void *) seed_vec);

    seed_vec = hash_seed_to_vec(hash_seed);
    if (!seed_vec) {
        LOG_ERROR("❌ Error: time_seed_to_vec() returned NULL!\n");
        free(seed_vec);
        return;
    }
    LOG_INFO("✅ Time-seeded vector generated: %p\n", (void *) seed_vec);

    // ✅ Check cp->SDR30 after initialization
    if (!cp) {
        LOG_ERROR("❌ Error: cp is still NULL after cp_init()!\n");
        return;
    }

    LOG_INFO("✅ Moving vector into MKRAND processor state...\n");
    vmov(seed_vec, cp->SDR30);
    mi5_time_quantum(cp);

    // ✅ Copy from SDR30 to `out` 
    vecbe_pack(out, cp->SDR30);
    LOG_INFO("✅ Successfully generated IPv6 block!\n");
}






