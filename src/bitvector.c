#include "bitvector.h"

create_c_list_type(uint64_t_list, uint64_t)

#define BITS_TO_WORDS(nBits) ((uint32_t) ((((int32_t)nBits) - 1) >> 6) + 1)

bitvector_t *bitvector_t_alloc(uint32_t nBits) {
  bitvector_t *bv = malloc(1 * sizeof(bitvector_t));
  bv->nBits = nBits;

  uint8_t ret = uint64_t_list_init(&bv->bits, BITS_TO_WORDS(bv->nBits), 0);
  if(ret != NO_ERROR) return NULL;

  bv->bits.nLength = BITS_TO_WORDS(bv->nBits);
  bitvector_t_clear(bv);
  return bv;
}

void bitvector_t_free(bitvector_t *bv) {
  uint64_t_list_free(&bv->bits, NULL);
  free(bv);
}

void bitvector_t_clear(bitvector_t *bv) {
  memset((void *)bv->bits.pList, 0, bv->bits.nLength * sizeof(uint64_t));
}

bitvector_t *bitvector_t_fromHexString(char *string, size_t length) {
  size_t i;
  char sc[2] = {0, 0};

  bitvector_t *bv = bitvector_t_alloc(length*4);
  if(bv == NULL) return NULL;

  for(i = 0; i < length; i++) {
    if(isxdigit(string[length-i - 1])) {
      sc[0] = string[length-i - 1];
      uint64_t digit = strtoul(sc, NULL, 16);
      bv->bits.pList[i>>4] |= digit << ((i&0xf)*4);
    } else break;
  }

  if(i != length) {
    fprintf(stderr, "Malformed string. Cannot convert to bitvector.\n");
    bitvector_t_free(bv);
    return NULL;
  }

  return bv;
}

char *bitvector_t_toHexString(bitvector_t *bv) {
  size_t i;
  if(bv == NULL) return NULL;

  uint32_t length = bv->nBits/4 + ((bv->nBits%4) != 0);

  char *string = malloc(length + 1);
  string[length] = 0;

  for(i = 0; i < length; i++) {
    char c[2];
    snprintf(c, 2, "%llx", (bv->bits.pList[i>>4] >> ((i&0xf)*4)) & 0xf);
    string[length-i - 1] = c[0];
  }

  return string;
}

bitvector_t *bitvector_t_copy(bitvector_t *bv) {
  bitvector_t *ret = bitvector_t_alloc(bv->nBits);
  if(uint64_t_list_copy(&ret->bits, &bv->bits) != NO_ERROR) {
    fprintf(stderr, "bitvector_t copy failed.\n");
    bitvector_t_free(ret);
    return NULL;
  }
  return ret;
}

void bitvector_t_widen(bitvector_t *bv, uint32_t nBits) {
  if(nBits < bv->nBits) {
    fprintf(stderr, "Cannot widen a bitvector_t of %d bits to %d bits.\n", bv->nBits, nBits);
    return;
  }
  bv->nBits = nBits;
  size_t length = bv->bits.nLength;
  uint64_t_list_resize(&bv->bits, BITS_TO_WORDS(nBits));
  bv->bits.nLength = BITS_TO_WORDS(bv->nBits);

  memset((void *)(bv->bits.pList+length), 0, (bv->bits.nLength - length) * sizeof(uint64_t));
}

bitvector_t *bitvector_t_concat(bitvector_t *x, bitvector_t *y) {
  size_t start = y->nBits >> 6;
  size_t length = x->bits.nLength;
  size_t i;

  bitvector_t *ret = bitvector_t_copy(y);
  bitvector_t_widen(ret, x->nBits + y->nBits);
  ret->bits.pList[start] &= ~0 >> (64 - (y->nBits&0x3f));

  for(i = start; i < start + length; i++) {
    ret->bits.pList[i] |= x->bits.pList[i-start] << (y->nBits&0x3f);
    if(i+1 <= start+length) {
      ret->bits.pList[i+1] = x->bits.pList[i-start] >> (64 - (y->nBits&0x3f));
    }
  }

  return ret;
}

void bitvector_t_negate_update(bitvector_t *bv) {
  size_t i;
  for(i = 0; i < bv->bits.nLength; i++) {
    bv->bits.pList[i] = ~bv->bits.pList[i];
  }
}

bitvector_t *bitvector_t_negate(bitvector_t *bv) {
  bitvector_t *ret = bitvector_t_copy(bv);
  bitvector_t_negate_update(ret);
  return ret;
}

void bitvector_t_take_update(bitvector_t *bv, uint32_t nBits) {
  if(nBits > bv->bits.nLength) {
    fprintf(stderr, "Cannot take %u bits from a bitvector_t with only %u bits.\n", nBits, bv->nBits);
    return;
  }
  bv->nBits = nBits;
  bv->bits.nLength = BITS_TO_WORDS(nBits);

  //Clear the previous high bits
  bv->bits.pList[bv->bits.nLength-1] &= ~0 >> (64 - (bv->nBits&0x3f));
}

bitvector_t *bitvector_t_take(bitvector_t *bv, uint32_t nBits) {
  if(nBits > bv->bits.nLength) {
    fprintf(stderr, "Cannot take %u bits from a bitvector_t with only %u bits.\n", nBits, bv->nBits);
    return NULL;
  }
  size_t nLength = bv->bits.nLength;
  bv->bits.nLength = BITS_TO_WORDS(nBits);
  bitvector_t *ret = bitvector_t_copy(bv);
  bv->bits.nLength = nLength;
  return ret;
}

#define bitvector_t_zipWith_update(NAME, OP)                          \
void bitvector_t_##NAME##_update(bitvector_t *x, bitvector_t *y) {    \
  size_t i;                                                           \
  if(x->nBits != y->nBits) {                                          \
    fprintf(stderr, "Cannot NAME two vectors of different sizes.\n"); \
    return;                                                           \
  }                                                                   \
  for(i = 0; i < x->bits.nLength; i++) {                              \
    x->bits.pList[i] = x->bits.pList[i] OP y->bits.pList[i];          \
  }                                                                   \
}                                                                     \

#define bitvector_t_zipWith(NAME, OP)                                 \
bitvector_t *bitvector_t_##NAME(bitvector_t *x, bitvector_t *y) {     \
  if(x->nBits != y->nBits) {                                          \
    fprintf(stderr, "Cannot XOR two vectors of different sizes.\n");  \
    return NULL;                                                      \
  }                                                                   \
  bitvector_t *ret = bitvector_t_copy(x);                             \
  bitvector_t_##NAME##_update(ret, y);                                \
  return ret;                                                         \
}                                                                     \

bitvector_t_zipWith_update(XOR, ^)
bitvector_t_zipWith(XOR, ^)

bitvector_t_zipWith_update(EQU, ==)
bitvector_t_zipWith(EQU, ==)

bitvector_t_zipWith_update(OR, |)
bitvector_t_zipWith(OR, |)

bitvector_t_zipWith_update(AND, &)
bitvector_t_zipWith(AND, &)
