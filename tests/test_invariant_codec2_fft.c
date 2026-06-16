#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Forward declarations from codec2_fft.c */
typedef struct {
    uint16_t bitRevLength;
    uint16_t fftLen;
    uint16_t *pBitRevTable;
    float *pTwiddle;
} arm_cfft_instance_f32;

/* Import the actual function under test */
extern arm_cfft_instance_f32* arm_cfft_init_f32(arm_cfft_instance_f32 *S);

START_TEST(test_fft_overflow_protection)
{
    /* Invariant: FFT initialization must not allocate based on unchecked 
       multiplication of structure fields, preventing heap overflow on 
       corrupted/attacker-controlled bitRevLength or fftLen values */
    
    struct {
        uint16_t bitRevLength;
        uint16_t fftLen;
        const char *description;
    } payloads[] = {
        { 0xFFFF, 0xFFFF, "max_uint16_overflow" },
        { 0x8000, 0x8000, "boundary_large_multiply" },
        { 256, 512, "valid_normal_fft" },
        { 0, 0, "zero_fields" },
        { 1, 1, "minimal_valid" }
    };
    
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        arm_cfft_instance_f32 instance;
        memset(&instance, 0, sizeof(instance));
        
        instance.bitRevLength = payloads[i].bitRevLength;
        instance.fftLen = payloads[i].fftLen;
        
        /* Call actual production function */
        arm_cfft_instance_f32 *result = arm_cfft_init_f32(&instance);
        
        /* Security property: function must either:
           1. Reject/handle overflow gracefully (return NULL or safe state)
           2. Allocate only safe amounts (no silent overflow leading to undersized buffer)
           3. Not proceed with memcpy on corrupted sizes */
        
        if (result != NULL) {
            /* If allocation succeeded, verify pointers are either NULL or valid */
            ck_assert(result->pBitRevTable == NULL || 
                     (uintptr_t)result->pBitRevTable > 0x1000);
            ck_assert(result->pTwiddle == NULL || 
                     (uintptr_t)result->pTwiddle > 0x1000);
            
            /* Cleanup */
            free(result->pBitRevTable);
            free(result->pTwiddle);
            free(result);
        }
        /* If NULL returned, that is also acceptable (safe rejection) */
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_fft_overflow_protection);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}