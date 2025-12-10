

#ifndef __BITMAT_H__
#define __BITMAT_H__

#include <stdint.h>
#include <string.h>

#define BITMAP_ARRAY_SIZE 1
#define BITMAT_SIMPLE 1
typedef struct
{
    union
    {
        uint32_t bitmap[BITMAP_ARRAY_SIZE];
        uint32_t *bitmap_ptr;
    };
#ifndef BITMAT_SIMPLE
    uint32_t use_ptr : 1;
    uint32_t bit_num_max : 16; /* 最大支持的bit位计数 */
#endif
} bitmap_t;

static inline void bitmap_init(bitmap_t *pbitmap, uint32_t *pbitmap_buff, uint32_t bitmap_size)
{
    memset(pbitmap, 0, sizeof(bitmap_t));
#ifndef BITMAT_SIMPLE
    if (pbitmap_buff)
    {
        pbitmap->use_ptr = 1;
        pbitmap->bitmap_ptr = pbitmap_buff;
        memset(pbitmap->bitmap_ptr, 0, bitmap_size * sizeof(uint32_t));
        pbitmap->bit_num_max = bitmap_size * sizeof(uint32_t) * 8;
    }
    else
    {
        pbitmap->use_ptr = 0;
        memset(pbitmap->bitmap, 0, BITMAP_ARRAY_SIZE * sizeof(uint32_t));
        pbitmap->bit_num_max = BITMAP_ARRAY_SIZE * sizeof(uint32_t) * 8;
    }
#endif
}

#define bitmap_set(_pmap, _bitid)                                \
    do                                                           \
    {                                                            \
        if (_bitid != 0)                                         \
        {                                                        \
            (_pmap)->bitmap[0] |= 1 << ((_bitid) - 1);           \
        }                                                        \
        else                                                     \
        {                                                        \
            memset((_pmap)->bitmap, 0, sizeof((_pmap)->bitmap)); \
        }                                                        \
    } while (0)

#define bitmap_unset(_pmap, _bitid)                       \
    do                                                    \
    {                                                     \
        if (_bitid)                                       \
        {                                                 \
            (_pmap)->bitmap[0] &= ~(1 << ((_bitid) - 1)); \
        }                                                 \
    } while (0)

#define bitmap_is_set(_pmap, _bitid) ((_pmap)->bitmap[0] & 1 << ((_bitid) - 1))

static inline uint32_t bitmap_next(bitmap_t *pbitmap, uint32_t bit_id)
{
    uint32_t bit_index = bit_id;

    if (pbitmap->bitmap[0] >> bit_index)
    {
        for (uint32_t i = bit_index; i < 32; i++)
        {
            if (pbitmap->bitmap[0] & (1u << i))
            {
                return i + 1;
            }
        }
    }

    return 0;
}
static inline uint32_t bitmap_get_free(bitmap_t *pbitmap)
{
    for (uint32_t i = 0; i < 32; i++)
    {
        if (!(pbitmap->bitmap[0] & (1u << i)))
        {
            return i + 1;
        }
    }
    return 0;
}
static inline uint32_t bitmap_cnt(bitmap_t *pbitmap)
{
    uint32_t count = 0;
    if (pbitmap->bitmap[0])
    {
        for (uint32_t i = 0; i < 32; i++)
        {
            if (pbitmap->bitmap[0] & (1u << i))
            {
                count++;
            }
        }
    }
    return count;
}

static inline uint32_t bitmap_cmp(bitmap_t *pbitmap1, bitmap_t *pbitmap2)
{
    for (uint32_t i = 0; i < 32; i++)
    {
        if ((pbitmap1->bitmap[0] & (1u << i)) != (pbitmap2->bitmap[0] & (1u << i)))
        {
            return i + 1;
        }
    }
    return 0;
}

static inline void bitmap_copy(bitmap_t *pbitmap_dest, const bitmap_t *pbitmap_src)
{
    memcpy(pbitmap_dest, pbitmap_src, sizeof(bitmap_t));
}
#endif