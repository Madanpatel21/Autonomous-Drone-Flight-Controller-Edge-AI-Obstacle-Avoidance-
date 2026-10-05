/* dump_param_defaults.c - manufacturing parameter default file generator (MFG-001).
 *
 * Why this exists: MFG-001 requires a "parameter default file" in the release
 * package. The defaults are NOT re-typed into a data file by hand - that would
 * let the file drift away from the firmware. This program links the firmware's
 * own parameters.c and crc16.c, calls params_defaults() and serialises exactly
 * the bytes params_store() would write: [struct bytes][crc16 u16 LE].
 *
 * It also emits the field layout (offsets from offsetof(), not from a hardcoded
 * struct in a comment) so audit_release_manifest.py can re-derive every field
 * and the CRC independently instead of trusting this program's word for it.
 *
 * Usage: dump_param_defaults <output_dir>
 */
#include "parameters.h"
#include "crc16.h"
#include "hal_interfaces.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ---- HAL stubs -----------------------------------------------------------
 * params_defaults() must never touch the backend. Any accidental call is a
 * hard failure rather than a silent default write, so a future edit that makes
 * the default path depend on flash is caught here. */
static unsigned g_flash_calls;

hal_status_t hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{ (void)addr; (void)buf; (void)len; g_flash_calls++; return HAL_ERROR; }
hal_status_t hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{ (void)addr; (void)buf; (void)len; g_flash_calls++; return HAL_ERROR; }
hal_status_t hal_flash_erase(uint32_t addr)
{ (void)addr; g_flash_calls++; return HAL_ERROR; }

/* ---- layout description -------------------------------------------------- */
typedef struct { const char *name; const char *type; size_t offset; } field_t;

static const field_t FIELDS[] = {
    { "magic",        "u32", offsetof(params_t, magic) },
    { "version",      "u16", offsetof(params_t, version) },
    { "rate_roll_kp", "f32", offsetof(params_t, rate_roll_kp) },
    { "rate_roll_ki", "f32", offsetof(params_t, rate_roll_ki) },
    { "rate_roll_kd", "f32", offsetof(params_t, rate_roll_kd) },
    { "rate_pitch_kp","f32", offsetof(params_t, rate_pitch_kp) },
    { "rate_pitch_ki","f32", offsetof(params_t, rate_pitch_ki) },
    { "rate_pitch_kd","f32", offsetof(params_t, rate_pitch_kd) },
    { "rate_yaw_kp",  "f32", offsetof(params_t, rate_yaw_kp) },
    { "rate_yaw_ki",  "f32", offsetof(params_t, rate_yaw_ki) },
    { "rate_yaw_kd",  "f32", offsetof(params_t, rate_yaw_kd) },
    { "batt_warn_v",  "f32", offsetof(params_t, batt_warn_v) },
    { "batt_rtl_v",   "f32", offsetof(params_t, batt_rtl_v) },
    { "batt_land_v",  "f32", offsetof(params_t, batt_land_v) },
};
#define NFIELDS (sizeof(FIELDS) / sizeof(FIELDS[0]))

static int write_blob(const char *dir, const params_t *p)
{
    uint8_t blob[sizeof(params_t) + sizeof(uint16_t)];
    memcpy(blob, p, sizeof(*p));
    const uint16_t crc = params_crc(p);
    memcpy(blob + sizeof(*p), &crc, sizeof(crc));

    char path[1024];
    snprintf(path, sizeof(path), "%s/params_defaults.bin", dir);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    const size_t n = fwrite(blob, 1, sizeof(blob), f);
    fclose(f);
    if (n != sizeof(blob)) { fprintf(stderr, "short write to %s\n", path); return 1; }
    printf("wrote %s (%u bytes)\n", path, (unsigned)sizeof(blob));
    return 0;
}

static int write_json(const char *dir, const params_t *p)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/params_defaults.json", dir);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }

    fprintf(f, "{\n");
    fprintf(f, "  \"generator\": \"dump_param_defaults.c (links firmware parameters.c)\",\n");
    fprintf(f, "  \"struct_size\": %u,\n", (unsigned)sizeof(params_t));
    fprintf(f, "  \"blob_len\": %u,\n", (unsigned)(sizeof(params_t) + sizeof(uint16_t)));
    fprintf(f, "  \"crc\": %u,\n", (unsigned)params_crc(p));
    fprintf(f, "  \"field_layout\": [\n");
    for (size_t i = 0; i < NFIELDS; i++)
        fprintf(f, "    { \"name\": \"%s\", \"type\": \"%s\", \"offset\": %u }%s\n",
                FIELDS[i].name, FIELDS[i].type, (unsigned)FIELDS[i].offset,
                (i + 1 < NFIELDS) ? "," : "");
    fprintf(f, "  ],\n");
    fprintf(f, "  \"fields\": {\n");
    for (size_t i = 0; i < NFIELDS; i++) {
        const char *n = FIELDS[i].name;
        const void *base = (const void *)p;
        fprintf(f, "    \"%s\": ", n);
        if (!strcmp(FIELDS[i].type, "u32"))
            fprintf(f, "%u", *(const uint32_t *)((const uint8_t *)base + FIELDS[i].offset));
        else if (!strcmp(FIELDS[i].type, "u16"))
            fprintf(f, "%u", *(const uint16_t *)((const uint8_t *)base + FIELDS[i].offset));
        else
            fprintf(f, "%.9g", (double)*(const float *)((const uint8_t *)base + FIELDS[i].offset));
        fprintf(f, "%s\n", (i + 1 < NFIELDS) ? "," : "");
    }
    fprintf(f, "  }\n}\n");
    fclose(f);
    printf("wrote %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: %s <output_dir>\n", argv[0]); return 2; }

    params_t p;
    params_defaults(&p);           /* firmware-owned defaults: no duplication */

    if (p.magic != PARAM_MAGIC) {
        fprintf(stderr, "params_defaults produced magic 0x%08X, expected 0x%08X\n",
                (unsigned)p.magic, (unsigned)PARAM_MAGIC);
        return 1;
    }
    if (g_flash_calls != 0) {
        fprintf(stderr, "default path touched the flash backend (%u calls): "
                        "the released file would depend on device state\n", g_flash_calls);
        return 1;
    }
    if (write_blob(argv[1], &p) != 0) return 1;
    if (write_json(argv[1], &p) != 0) return 1;
    return 0;
}
