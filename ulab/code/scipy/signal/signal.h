
/*
 * This file is part of the micropython-ulab project,
 *
 * https://github.com/v923z/micropython-ulab
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2021 Zoltán Vörös
 * 
 * Edited 2026 Rajarshi Das
 *
*/

#ifndef _SCIPY_SIGNAL_
#define _SCIPY_SIGNAL_

#include "../../ulab.h"
#include "../../ndarray.h"

extern const mp_obj_module_t ulab_scipy_signal_module;

#if ULAB_SCIPY_SIGNAL_HAS_SOSFILT
MP_DECLARE_CONST_FUN_OBJ_KW(signal_sosfilt_obj);
#endif

//Custom || STFT Implementation
//========
#if ULAB_SCIPY_SIGNAL_HAS_STFT
MP_DECLARE_CONST_FUN_OBJ_KW(signal_stft_obj);
#endif
//========

#endif /* _SCIPY_SIGNAL_ */

