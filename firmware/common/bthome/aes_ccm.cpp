// AES-CCM-128 wrapper backed by mbedtls.
//
// The same source compiles for:
//   - ESP-IDF builds (ESP-IDF ships mbedtls; just `REQUIRES mbedtls` in the
//     component CMakeLists)
//   - Zephyr / nRF Connect SDK builds (CONFIG_MBEDTLS=y +
//     CONFIG_MBEDTLS_CIPHER_CCM_ENABLED=y in prj.conf)
//   - Native host tests (link against system mbedtls — `brew install mbedtls`
//     on macOS, `apt install libmbedtls-dev` on Debian)

#include "aes_ccm.h"

#include <mbedtls/ccm.h>

namespace carl::aes_ccm {

namespace {

class CcmCtx {
public:
    explicit CcmCtx(const uint8_t key[kKeyLen]) : ok_(false) {
        mbedtls_ccm_init(&ctx_);
        ok_ = mbedtls_ccm_setkey(&ctx_, MBEDTLS_CIPHER_ID_AES,
                                 key, kKeyLen * 8) == 0;
    }
    ~CcmCtx() { mbedtls_ccm_free(&ctx_); }
    CcmCtx(const CcmCtx&) = delete;
    CcmCtx& operator=(const CcmCtx&) = delete;

    mbedtls_ccm_context* raw() { return &ctx_; }
    bool ok() const { return ok_; }

private:
    mbedtls_ccm_context ctx_;
    bool ok_;
};

}  // namespace

bool encrypt(const uint8_t key[kKeyLen],
             const uint8_t nonce[kNonceLen],
             const uint8_t* pt, size_t pt_len,
             uint8_t* ct,
             uint8_t mic[kMicLen]) {
    CcmCtx c(key);
    if (!c.ok()) return false;
    return mbedtls_ccm_encrypt_and_tag(c.raw(), pt_len,
                                       nonce, kNonceLen,
                                       /*add=*/nullptr, /*add_len=*/0,
                                       pt, ct,
                                       mic, kMicLen) == 0;
}

bool decrypt(const uint8_t key[kKeyLen],
             const uint8_t nonce[kNonceLen],
             const uint8_t* ct, size_t ct_len,
             uint8_t* pt,
             const uint8_t mic[kMicLen]) {
    CcmCtx c(key);
    if (!c.ok()) return false;
    return mbedtls_ccm_auth_decrypt(c.raw(), ct_len,
                                    nonce, kNonceLen,
                                    /*add=*/nullptr, /*add_len=*/0,
                                    ct, pt,
                                    mic, kMicLen) == 0;
}

}  // namespace carl::aes_ccm
