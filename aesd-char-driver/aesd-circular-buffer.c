/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    // find which entry the fpos maps to, wrt out_offs
    struct aesd_buffer_entry * p_entry = NULL;
    size_t current_string_size = 0;
    uint8_t offset = buffer->out_offs;
    do
    {
        current_string_size += buffer->entry[offset].size;
        // if char_offset is less that the current string size we have found the offset 
        // in the buffer containt the char_offset
        if (char_offset <= current_string_size - 1)
        {
            p_entry = &buffer->entry[offset];

            // to find entry_offset_byte_rtn we need the current_string_size before current 
            // element was added to it, and then we can subtract that from char_offset
            *entry_offset_byte_rtn = char_offset - (current_string_size - buffer->entry[offset].size);
            break;
        }
        offset = (offset + 1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    } while (offset != buffer->in_offs);

    return p_entry;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
   // write data
    buffer->entry[buffer->in_offs] = *add_entry;
    if (buffer->full)
    {
        // if buffer is full, we have overwritten the oldest data, so update out_offs
        buffer->out_offs = (buffer->out_offs + 1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
    }

    // move in_offs to next location in buffer
    buffer->in_offs = (buffer->in_offs + 1)%AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    // update full flag
    buffer->full = (buffer->in_offs == buffer->out_offs)? true : false;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
