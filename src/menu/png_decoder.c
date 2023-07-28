#include <libspng/spng/spng.h>

#include "path.h"
#include "png_decoder.h"


typedef struct {
    FILE *file;

    spng_ctx *ctx;
    struct spng_ihdr ihdr;

    surface_t *image;
    uint8_t *row_buffer;

    png_callback_t *callback;
    void *callback_data;
} png_decoder_t;

static png_decoder_t *decoder;


static void png_decoder_deinit (bool free_image) {
    if (decoder != NULL) {
        if (decoder->file != NULL) {
            fclose(decoder->file);
        }
        if (decoder->ctx != NULL) {
            spng_ctx_free(decoder->ctx);
        }
        if ((decoder->image != NULL) && free_image) {
            surface_free(decoder->image);
            free(decoder->image);
        }
        if (decoder->row_buffer != NULL) {
            free(decoder->row_buffer);
        }
        free(decoder);
        decoder = NULL;
    }
}


png_err_t png_decode_start (char *path, int max_width, int max_height, png_callback_t *callback, void *callback_data) {
    path_t *file_path;
    size_t image_size;

    if (decoder != NULL) {
        return PNG_ERR_BUSY;
    }

    decoder = calloc(1, sizeof(png_decoder_t));
    if (decoder == NULL) {
        return PNG_ERR_OUT_OF_MEM;
    }

    file_path = path_init("sd:/");
    path_append(file_path, path);
    decoder->file = fopen(path_get(file_path), "r");
    path_free(file_path);
    if (decoder->file == NULL) {
        png_decoder_deinit(false);
        return PNG_ERR_NO_FILE;
    }

    if ((decoder->ctx = spng_ctx_new(SPNG_CTX_IGNORE_ADLER32)) == NULL) {
        png_decoder_deinit(false);
        return PNG_ERR_OUT_OF_MEM;
    }

    if (spng_set_crc_action(decoder->ctx, SPNG_CRC_USE, SPNG_CRC_USE) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_INT;
    }

    if (spng_set_image_limits(decoder->ctx, max_width, max_height) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_INT;
    }

    if (spng_set_png_file(decoder->ctx, decoder->file) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_INT;
    }

    if (spng_decoded_image_size(decoder->ctx, SPNG_FMT_RGB8, &image_size) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_BAD_FILE;
    }

    if (spng_decode_image(decoder->ctx, NULL, image_size, SPNG_FMT_RGB8, SPNG_DECODE_PROGRESSIVE) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_BAD_FILE;
    }

    if (spng_get_ihdr(decoder->ctx, &decoder->ihdr) != SPNG_OK) {
        png_decoder_deinit(false);
        return PNG_ERR_BAD_FILE;
    }

    decoder->image = calloc(1, sizeof(surface_t));
    if (decoder->image == NULL) {
        png_decoder_deinit(false);
        return PNG_ERR_OUT_OF_MEM;
    }

    *decoder->image = surface_alloc(FMT_RGBA16, decoder->ihdr.width, decoder->ihdr.height);
    if (decoder->image->buffer == NULL) {
        png_decoder_deinit(true);
        return PNG_ERR_OUT_OF_MEM;
    }

    if ((decoder->row_buffer = malloc(decoder->ihdr.width * 3)) == NULL) {
        png_decoder_deinit(true);
        return PNG_ERR_OUT_OF_MEM;
    }

    decoder->callback = callback;
    decoder->callback_data = callback_data;

    return PNG_OK;
}

void png_decode_abort (void) {
    png_decoder_deinit(true);
}

void png_poll (void) {
    if (decoder) {        
        enum spng_errno err;
        struct spng_row_info row_info;

        if ((err = spng_get_row_info(decoder->ctx, &row_info)) != SPNG_OK) {
            decoder->callback(PNG_ERR_BAD_FILE, NULL, decoder->callback_data);
            png_decoder_deinit(true);
            return;
        }

        err = spng_decode_row(decoder->ctx, decoder->row_buffer, decoder->ihdr.width * 3);

        if (err == SPNG_OK || err == SPNG_EOI) {
            uint16_t *image_buffer = decoder->image->buffer + (row_info.row_num * decoder->image->stride);
            for (int i = 0; i < decoder->ihdr.width * 3; i += 3) {
                uint8_t r = decoder->row_buffer[i + 0] >> 3;
                uint8_t g = decoder->row_buffer[i + 1] >> 3;
                uint8_t b = decoder->row_buffer[i + 2] >> 3;
                *image_buffer++ = (r << 11) | (g << 6) | (b << 1) | 1;
            }
        }

        if (err == SPNG_EOI) {
            decoder->callback(PNG_OK, decoder->image, decoder->callback_data);
            png_decoder_deinit(false);
        } else if (err != SPNG_OK) {
            decoder->callback(PNG_ERR_BAD_FILE, NULL, decoder->callback_data);
            png_decoder_deinit(true);
        }
    }
}