// SPDX-FileCopyrightText: 2022 Peiwei Hu <jlu.hpw@foxmail.com>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_core.h>

#define STRING_CHUNK 16

/**
 * Return a C/C++ string defination with block size as the length
 * \param core RzCore
 * \return a string defination or NULL if the error happens
 */
RZ_API char *rz_core_print_string_c_cpp(RzCore *core) {
	ut64 value;
	size_t size = core->blocksize;
	RzStrBuf *sb = rz_strbuf_new(NULL);

	if (!sb) {
		RZ_LOG_ERROR("Fail to allocate the memory\n");
		return NULL;
	}
	rz_strbuf_appendf(sb, "#define STRING_SIZE %" PFMTSZd "\nconst char s[STRING_SIZE] = \"", size);
	for (size_t pos = 0; pos < size; pos++) {
		if (pos && !(pos % STRING_CHUNK)) {
			// newline and padding for long string
			rz_strbuf_appendf(sb, "\"\n                            \"");
		}
		value = rz_read_ble(core->block + pos, false, 8);
		rz_strbuf_appendf(sb, "\\x%02" PFMT64x, value);
	}
	rz_strbuf_append(sb, "\";");
	return rz_strbuf_drain(sb);
}

/**
 * \brief Print hexdump diff between \p aa and \p ba with \p len
 */
RZ_API bool rz_core_print_cmp(RZ_NONNULL RzCore *core, ut64 aa, ut64 ba, ut64 len) {
	rz_return_val_if_fail(core && core->cons && len > 0, false);
	ut8 *a = malloc(len);
	if (!a) {
		return false;
	}
	ut8 *b = malloc(len);
	if (!b) {
		free(a);
		return false;
	}

	RZ_LOG_VERBOSE("diff 0x%" PFMT64x " 0x%" PFMT64x " with len:%" PFMT64d "\n", aa, ba, len);

	rz_io_read_at(core->io, aa, a, (int)len);
	rz_io_read_at(core->io, ba, b, (int)len);
	int col = core->cons->columns > 123;
	rz_print_hexdiff(core->print, aa, a,
		ba, b, (int)len, col);
	free(a);
	free(b);
	return true;
}

static inline st8 format_type_to_base(const RzCorePrintFormatType format, const ut8 n) {
	static const st8 bases[][9] = {
		{ 0, 8 },
		{ 0, -1, -10, [4] = 10, [8] = -8 },
		{ 0, 16, 32, [4] = 32, [8] = 64 },
	};
	if (format >= RZ_CORE_PRINT_FORMAT_TYPE_INVALID || n >= sizeof(bases[0])) {
		return 0;
	}
	return bases[format][n];
}

static inline void fix_size_from_format(const RzCorePrintFormatType format, ut8 *size) {
	if (format != RZ_CORE_PRINT_FORMAT_TYPE_INTEGER) {
		return;
	}
	static const st8 sizes[] = {
		0, 4, 2, [4] = 4, [8] = 4
	};
	if (*size >= sizeof(sizes)) {
		return;
	}
	*size = sizes[*size];
}

static inline void len_fixup(RzCore *core, ut64 *addr, int *len) {
	if (!len || *len >= 0) {
		return;
	}
	*len = -*len;
	if (*len > core->blocksize_max) {
		RZ_LOG_ERROR("this block size is too big (0x%" PFMT32x
			     " < 0x%" PFMT32x ").",
			*len,
			core->blocksize_max);
		*len = (int)core->blocksize_max;
	}
	if (addr) {
		*addr = *addr - *len;
	}
}

/**
 * \brief Print dump at \p addr
 * \param n Word size by bytes (1,2,4,8)
 * \param len Dump bytes length
 * \param format Print format, such as RZ_CORE_PRINT_FORMAT_TYPE_HEXADECIMAL
 */
RZ_API bool rz_core_print_dump(RZ_NONNULL RzCore *core, RZ_NULLABLE RzCmdStateOutput *state,
	ut64 addr, ut8 n, int len, const RzCorePrintFormatType format) {
	rz_return_val_if_fail(core, false);
	if (!len) {
		return true;
	}
	st8 base = format_type_to_base(format, n);
	if (!base) {
		return false;
	}
	len_fixup(core, &addr, &len);
	ut8 *buffer = malloc(len);
	if (!buffer) {
		return false;
	}

	rz_io_read_at(core->io, addr, buffer, len);
	rz_print_init_rowoffsets(core->print);
	core->print->use_comments = false;
	RzOutputMode mode = state ? state->mode : RZ_OUTPUT_MODE_STANDARD;
	switch (mode) {
	case RZ_OUTPUT_MODE_JSON:
		rz_print_jsondump(core->print, buffer, len, n * 8);
		break;
	case RZ_OUTPUT_MODE_STANDARD:
		fix_size_from_format(format, &n);
		rz_print_hexdump(core->print, addr,
			buffer, len, base, (int)n, 1);
		break;
	default:
		rz_warn_if_reached();
		free(buffer);
		return false;
	}
	free(buffer);
	return true;
}

/**
 * \brief Print hexdump at \p addr, but maybe print hexdiff if (diff.from or diff.to), \see "el diff"
 * \param len Dump bytes length
 */
RZ_API bool rz_core_print_hexdump_(RZ_NONNULL RzCore *core, RZ_NULLABLE RzCmdStateOutput *state, ut64 addr, int len) {
	rz_return_val_if_fail(core, false);
	if (!len) {
		return true;
	}

	RzOutputMode mode = state ? state->mode : RZ_OUTPUT_MODE_STANDARD;
	switch (mode) {
	case RZ_OUTPUT_MODE_STANDARD: {
		ut64 from = rz_config_get_i(core->config, "diff.from");
		ut64 to = rz_config_get_i(core->config, "diff.to");
		if (from == to && !from) {
			len_fixup(core, &addr, &len);
			ut8 *buffer = malloc(len);
			if (!buffer) {
				return false;
			}
			rz_io_read_at(core->io, addr, buffer, len);
			rz_print_hexdump(core->print, rz_core_pava(core, addr), buffer, len, 16, 1, 1);
			free(buffer);
		} else {
			rz_core_print_cmp(core, addr, addr + to - from, len);
		}
		break;
	}
	case RZ_OUTPUT_MODE_JSON:
		rz_print_jsondump(core->print, core->block, len, 8);
		break;
	default:
		rz_warn_if_reached();
		return false;
	}
	return true;
}

static inline char *ut64_to_hex(const ut64 x, const ut8 width) {
	RzStrBuf *sb = rz_strbuf_new(NULL);
	rz_strbuf_appendf(sb, "%" PFMT64x, x);
	ut8 len = rz_strbuf_length(sb);
	if (len < width) {
		rz_strbuf_prepend(sb, rz_str_pad('0', width - len));
	}
	rz_strbuf_prepend(sb, "0x");
	return rz_strbuf_drain(sb);
}

/**
 * \brief Hexdump at \p addr
 * \param len Dump bytes length
 * \param size Word size by bytes (1,2,4,8)
 * \return Hexdump string
 */
RZ_API RZ_OWN char *rz_core_print_hexdump_byline(RZ_NONNULL RzCore *core, RZ_NULLABLE RzCmdStateOutput *state,
	ut64 addr, int len, ut8 size) {
	rz_return_val_if_fail(core, false);
	if (!len) {
		return NULL;
	}
	len_fixup(core, &addr, &len);
	ut8 *buffer = malloc(len);
	if (!buffer) {
		return NULL;
	}

	rz_io_read_at(core->io, addr, buffer, len);
	const int round_len = len - (len % size);
	bool hex_offset = (!(state && state->mode == RZ_OUTPUT_MODE_QUIET) && rz_config_get_i(core->config, "hex.offset"));
	RzStrBuf *sb = rz_strbuf_new(NULL);
	for (int i = 0; i < round_len; i += size) {
		const char *a, *b;
		char *fn;
		RzPrint *p = core->print;
		RzFlagItem *f;
		ut64 v = rz_read_ble(buffer + i, p->big_endian, size * 8);
		if (p && p->colorfor) {
			a = p->colorfor(p->user, v, true);
			if (a && *a) {
				b = Color_RESET;
			} else {
				a = b = "";
			}
		} else {
			a = b = "";
		}
		f = rz_flag_get_at(core->flags, v, true);
		fn = NULL;
		if (f) {
			st64 delta = (st64)(v - f->offset);
			if (delta >= 0 && delta < 8192) {
				if (v == f->offset) {
					fn = strdup(f->name);
				} else {
					fn = rz_str_newf("%s+%" PFMT64d, f->name, v - f->offset);
				}
			}
		}
		char *vstr = ut64_to_hex(v, size * 2);
		if (vstr) {
			if (hex_offset) {
				rz_print_section(core->print, addr + i);
				rz_strbuf_appendf(sb, "0x%08" PFMT64x " %s%s%s%s%s\n",
					(ut64)addr + i, a, vstr, b, fn ? " " : "", fn ? fn : "");
			} else {
				rz_strbuf_appendf(sb, "%s%s%s\n", a, vstr, b);
			}
		}
		free(vstr);
		free(fn);
	}
	free(buffer);
	return rz_strbuf_drain(sb);
}
