
/*
 * This file is part of the micropython-ulab project,
 *
 * https://github.com/v923z/micropython-ulab
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jeff Epler for Adafruit Industries
 *               2020 Scott Shawcroft for Adafruit Industries
 *               2020-2021 Zoltán Vörös
 *               2020 Taku Fukada
 * 
 * Edited 2026 Rajarshi Das
*/

#include <math.h>
#include <string.h>
#include "py/runtime.h"

#include "../../ulab.h"
#include "../../ndarray.h"
#include "../../numpy/carray/carray_tools.h"

#if ULAB_SCIPY_SIGNAL_HAS_SOSFILT & ULAB_MAX_DIMS > 1
static void signal_sosfilt_array(mp_float_t *x, const mp_float_t *coeffs, mp_float_t *zf, const size_t len) {
    for(size_t i=0; i < len; i++) {
        mp_float_t xn = *x;
        *x = coeffs[0] * xn + zf[0];
        zf[0] = zf[1] + coeffs[1] * xn - coeffs[4] * *x;
        zf[1] = coeffs[2] * xn - coeffs[5] * *x;
        x++;
    }
    x -= len;
}

//Custom || STFT Implementation
//========
#if ULAB_SCIPY_SIGNAL_HAS_STFT

// Include FFT headers
#include "../../numpy/fft/fft_tools.h"

// Helper: Generate Hann window
static void generate_hann_window(mp_float_t *window, size_t n) {
    for(size_t i = 0; i < n; i++) {
        window[i] = 0.5 * (1.0 - MICROPY_FLOAT_C_FUN(cos)(2.0 * MP_PI * i / (n - 1)));
    }
}

mp_obj_t signal_stft(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_nperseg, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 256 } },
        { MP_QSTR_noverlap, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_sample_rate, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE } },  // NEW!
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    // Validate input
    if(!mp_obj_is_type(args[0].u_obj, &ulab_ndarray_type)) {
        mp_raise_TypeError(MP_ERROR_TEXT("input must be an ndarray"));
    }
    
    ndarray_obj_t *input = MP_OBJ_TO_PTR(args[0].u_obj);
    size_t input_len = input->len;
    
    // Get parameters
    size_t nperseg = (size_t)args[1].u_int;
    size_t noverlap = nperseg / 2;
    if(args[2].u_obj != mp_const_none) {
        noverlap = (size_t)mp_obj_get_int(args[2].u_obj);
    }
    
    // Get sample_rate parameter (NEW!)
    mp_float_t fs = 1.0;  // Default
    if(args[3].u_obj != mp_const_none) {
        fs = mp_obj_get_float(args[3].u_obj);
    }
    
    if(noverlap >= nperseg) {
        mp_raise_ValueError(MP_ERROR_TEXT("noverlap must be less than nperseg"));
    }
    
    size_t hop = nperseg - noverlap;
    
    // Boundary extension (zero padding)
    size_t pad_len = nperseg / 2;
    size_t extended_len = input_len + 2 * pad_len;
    
    // Allocate extended signal
    mp_float_t *x_extended = m_new(mp_float_t, extended_len);
    
    // Zero pad at start
    for(size_t i = 0; i < pad_len; i++) {
        x_extended[i] = 0.0;
    }
    
    // Copy input data (convert to float)
    uint8_t *input_data = (uint8_t *)input->array;
    for(size_t i = 0; i < input_len; i++) {
        x_extended[pad_len + i] = ndarray_get_float_value(input_data, input->dtype);
        input_data += input->strides[ULAB_MAX_DIMS - 1];
    }
    
    // Zero pad at end
    for(size_t i = pad_len + input_len; i < extended_len; i++) {
        x_extended[i] = 0.0;
    }
    
    // Calculate dimensions
    size_t n_frames = (extended_len - nperseg) / hop + 1;
    size_t n_freqs = nperseg / 2 + 1;
    
    // Generate Hann window
    mp_float_t *window = m_new(mp_float_t, nperseg);
    generate_hann_window(window, nperseg);
    
    // Calculate window sum for scaling
    mp_float_t win_sum = 0.0;
    for(size_t i = 0; i < nperseg; i++) {
        win_sum += window[i];
    }
    mp_float_t scale = 1.0 / win_sum;
    
    // Allocate output STFT array (frequencies x time)
    size_t *shape = ndarray_shape_vector(0, 0, n_freqs, n_frames);
    ndarray_obj_t *stft_result = ndarray_new_dense_ndarray(2, shape, NDARRAY_COMPLEX);
    mp_float_t *stft_data = (mp_float_t *)stft_result->array;
    
    // Allocate FFT buffer once (complex format: interleaved real/imag)
    mp_float_t *fft_buffer = m_new(mp_float_t, nperseg * 2);
    
    // Process each frame
    for(size_t frame_idx = 0; frame_idx < n_frames; frame_idx++) {
        size_t start = frame_idx * hop;
        
        // Apply window and pack into complex format
        for(size_t i = 0; i < nperseg; i++) {
            fft_buffer[2*i] = x_extended[start + i] * window[i];
            fft_buffer[2*i + 1] = 0.0;
        }
        
        // Call FFT kernel directly
        #if ULAB_SUPPORTS_COMPLEX & ULAB_FFT_IS_NUMPY_COMPATIBLE
        fft_kernel(fft_buffer, nperseg, 1);
        #else
        mp_raise_NotImplementedError(MP_ERROR_TEXT("STFT requires complex support"));
        #endif
        
        // Copy positive frequencies and apply scaling
        for(size_t k = 0; k < n_freqs; k++) {
            size_t idx = (k * n_frames + frame_idx) * 2;
            stft_data[idx] = fft_buffer[2*k] * scale;
            stft_data[idx + 1] = fft_buffer[2*k + 1] * scale;
        }
    }
    
    // Clean up
    m_del(mp_float_t, fft_buffer, nperseg * 2);
    m_del(mp_float_t, x_extended, extended_len);
    m_del(mp_float_t, window, nperseg);
    
    // Create frequency array (NOW USING fs!)
    ndarray_obj_t *freqs = ndarray_new_linear_array(n_freqs, NDARRAY_FLOAT);
    mp_float_t *freq_data = (mp_float_t *)freqs->array;
    for(size_t i = 0; i < n_freqs; i++) {
        freq_data[i] = i * fs / nperseg;  // Uses fs now!
    }
    
    // Create time array (NOW USING fs!)
    ndarray_obj_t *times = ndarray_new_linear_array(n_frames, NDARRAY_FLOAT);
    mp_float_t *time_data = (mp_float_t *)times->array;
    for(size_t i = 0; i < n_frames; i++) {
        time_data[i] = ((mp_float_t)(i * hop) - (mp_float_t)pad_len) / fs;  // Uses fs now!
    }
    
    // Return tuple (freqs, times, Zxx)
    mp_obj_tuple_t *result = MP_OBJ_TO_PTR(mp_obj_new_tuple(3, NULL));
    result->items[0] = MP_OBJ_FROM_PTR(freqs);
    result->items[1] = MP_OBJ_FROM_PTR(times);
    result->items[2] = MP_OBJ_FROM_PTR(stft_result);
    
    return MP_OBJ_FROM_PTR(result);
}

MP_DEFINE_CONST_FUN_OBJ_KW(signal_stft_obj, 1, signal_stft);
#endif
//========

mp_obj_t signal_sosfilt(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_sos, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_x, MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE } },
        { MP_QSTR_zi, MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE } },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    if(!ndarray_object_is_array_like(args[0].u_obj) || !ndarray_object_is_array_like(args[1].u_obj)) {
        mp_raise_TypeError(MP_ERROR_TEXT("sosfilt requires iterable arguments"));
    }
    #if ULAB_SUPPORTS_COMPLEX
    if(mp_obj_is_type(args[1].u_obj, &ulab_ndarray_type)) {
        ndarray_obj_t *ndarray = MP_OBJ_TO_PTR(args[1].u_obj);
        COMPLEX_DTYPE_NOT_IMPLEMENTED(ndarray->dtype)
    }
    #endif
    size_t lenx = (size_t)mp_obj_get_int(mp_obj_len_maybe(args[1].u_obj));
    ndarray_obj_t *y = ndarray_new_linear_array(lenx, NDARRAY_FLOAT);
    mp_float_t *yarray = (mp_float_t *)y->array;
    mp_float_t coeffs[6];
    if(mp_obj_is_type(args[1].u_obj, &ulab_ndarray_type)) {
        ndarray_obj_t *inarray = MP_OBJ_TO_PTR(args[1].u_obj);
        #if ULAB_MAX_DIMS > 1
        if(inarray->ndim > 1) {
            mp_raise_ValueError(MP_ERROR_TEXT("input must be one-dimensional"));
        }
        #endif
        uint8_t *iarray = (uint8_t *)inarray->array;
        for(size_t i=0; i < lenx; i++) {
            *yarray++ = ndarray_get_float_value(iarray, inarray->dtype);
            iarray += inarray->strides[ULAB_MAX_DIMS - 1];
        }
        yarray -= lenx;
    } else {
        fill_array_iterable(yarray, args[1].u_obj);
    }

    mp_obj_iter_buf_t iter_buf;
    mp_obj_t item, iterable = mp_getiter(args[0].u_obj, &iter_buf);
    size_t lensos = (size_t)mp_obj_get_int(mp_obj_len_maybe(args[0].u_obj));

    size_t *shape = ndarray_shape_vector(0, 0, lensos, 2);
    ndarray_obj_t *zf = ndarray_new_dense_ndarray(2, shape, NDARRAY_FLOAT);
    mp_float_t *zf_array = (mp_float_t *)zf->array;

    if(args[2].u_obj != mp_const_none) {
        if(!mp_obj_is_type(args[2].u_obj, &ulab_ndarray_type)) {
            mp_raise_TypeError(MP_ERROR_TEXT("zi must be an ndarray"));
        } else {
            ndarray_obj_t *zi = MP_OBJ_TO_PTR(args[2].u_obj);
            if((zi->shape[ULAB_MAX_DIMS - 2] != lensos) || (zi->shape[ULAB_MAX_DIMS - 1] != 2)) {
                mp_raise_ValueError(MP_ERROR_TEXT("zi must be of shape (n_section, 2)"));
            }
            if(zi->dtype != NDARRAY_FLOAT) {
                mp_raise_ValueError(MP_ERROR_TEXT("zi must be of float type"));
            }
            // TODO: this won't work with sparse arrays
            memcpy(zf_array, zi->array, 2*lensos*sizeof(mp_float_t));
        }
    }
    while((item = mp_iternext(iterable)) != MP_OBJ_STOP_ITERATION) {
        if(mp_obj_get_int(mp_obj_len_maybe(item)) != 6) {
            mp_raise_ValueError(MP_ERROR_TEXT("sos array must be of shape (n_section, 6)"));
        } else {
            fill_array_iterable(coeffs, item);
            if(coeffs[3] != MICROPY_FLOAT_CONST(1.0)) {
                mp_raise_ValueError(MP_ERROR_TEXT("sos[:, 3] should be all ones"));
            }
            signal_sosfilt_array(yarray, coeffs, zf_array, lenx);
            zf_array += 2;
        }
    }
    if(args[2].u_obj == mp_const_none) {
        return MP_OBJ_FROM_PTR(y);
    } else {
        mp_obj_tuple_t *tuple = MP_OBJ_TO_PTR(mp_obj_new_tuple(2, NULL));
        tuple->items[0] = MP_OBJ_FROM_PTR(y);
        tuple->items[1] = MP_OBJ_FROM_PTR(zf);
        return MP_OBJ_FROM_PTR(tuple);
    }
}

MP_DEFINE_CONST_FUN_OBJ_KW(signal_sosfilt_obj, 2, signal_sosfilt);
#endif /* ULAB_SCIPY_SIGNAL_HAS_SOSFILT */

static const mp_rom_map_elem_t ulab_scipy_signal_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_signal) },
    #if ULAB_SCIPY_SIGNAL_HAS_SOSFILT & ULAB_MAX_DIMS > 1
        { MP_ROM_QSTR(MP_QSTR_sosfilt), MP_ROM_PTR(&signal_sosfilt_obj) },
    #endif

    //Custom || STFT Implementation
    //========
    #if ULAB_SCIPY_SIGNAL_HAS_STFT
        { MP_ROM_QSTR(MP_QSTR_stft), MP_ROM_PTR(&signal_stft_obj) },
    #endif
    //========
};

static MP_DEFINE_CONST_DICT(mp_module_ulab_scipy_signal_globals, ulab_scipy_signal_globals_table);

const mp_obj_module_t ulab_scipy_signal_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&mp_module_ulab_scipy_signal_globals,
};
#if CIRCUITPY_ULAB
MP_REGISTER_MODULE(MP_QSTR_ulab_dot_scipy_dot_signal, ulab_scipy_signal_module);
#endif
