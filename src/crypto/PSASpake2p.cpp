/*
 *    Copyright (c) 2023 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "PSASpake2p.h"

#include "CHIPCryptoPALPSA.h"

#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>

#include <psa/crypto.h>

namespace chip {
namespace Crypto {

CHIP_ERROR PSASpake2p_P256_SHA256_HKDF_HMAC::Init(const uint8_t * context, size_t context_len)
{
    Clear();

    VerifyOrReturnError(context_len <= sizeof(mContext), CHIP_ERROR_BUFFER_TOO_SMALL);

    psa_pake_cipher_suite_t cs = PSA_PAKE_CIPHER_SUITE_INIT;
    psa_pake_cs_set_algorithm(&cs, PSA_ALG_SPAKE2P);
    psa_pake_cs_set_primitive(&cs, PSA_PAKE_PRIMITIVE(PSA_PAKE_PRIMITIVE_TYPE_ECC, PSA_ECC_FAMILY_SECP_R1, 256));
    psa_pake_cs_set_hash(&cs, PSA_ALG_SHA_256);

    psa_status_t status = psa_pake_setup(&mOperation, &cs);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    memcpy(mContext, context, context_len);
    mContextLen = context_len;

    return CHIP_NO_ERROR;
}

void PSASpake2p_P256_SHA256_HKDF_HMAC::Clear()
{
    IgnoreUnusedVariable(psa_pake_abort(&mOperation));
    mOperation = psa_pake_operation_init();

    IgnoreUnusedVariable(psa_destroy_key(mKey));
    mKey = PSA_KEY_ID_NULL;
}

CHIP_ERROR PSASpake2p_P256_SHA256_HKDF_HMAC::BeginVerifier(const uint8_t * my_identity, size_t my_identity_len,
                                                           const uint8_t * peer_identity, size_t peer_identity_len,
                                                           const uint8_t * w0in, size_t w0in_len, const uint8_t * Lin,
                                                           size_t Lin_len)
{
    VerifyOrReturnError(w0in_len <= kSpake2p_WS_Length, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(Lin_len == kP256_Point_Length, CHIP_ERROR_INVALID_ARGUMENT);

    mRole               = PSA_PAKE_ROLE_SERVER;
    psa_status_t status = psa_pake_set_role(&mOperation, PSA_PAKE_ROLE_SERVER);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_set_peer(&mOperation, peer_identity, peer_identity_len);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_set_user(&mOperation, my_identity, my_identity_len);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    uint8_t password[kSpake2p_WS_Length + kP256_Point_Length];
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    memcpy(password + 0, w0in, w0in_len);
    memcpy(password + w0in_len, Lin, Lin_len);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_SPAKE2P);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_PASSWORD);

    status = psa_import_key(&attributes, password, w0in_len + Lin_len, &mKey);
    psa_reset_key_attributes(&attributes);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_set_password_key(&mOperation, mKey);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_input(&mOperation, PSA_PAKE_STEP_CONTEXT, mContext, mContextLen);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    return CHIP_NO_ERROR;
}

CHIP_ERROR PSASpake2p_P256_SHA256_HKDF_HMAC::BeginProver(const uint8_t * my_identity, size_t my_identity_len,
                                                         const uint8_t * peer_identity, size_t peer_identity_len,
                                                         const uint8_t * w0in, size_t w0in_len, const uint8_t * w1in,
                                                         size_t w1in_len)
{
    VerifyOrReturnError(w0in_len <= kSpake2p_WS_Length, CHIP_ERROR_INVALID_ARGUMENT);
    VerifyOrReturnError(w1in_len <= kSpake2p_WS_Length, CHIP_ERROR_INVALID_ARGUMENT);

    mRole               = PSA_PAKE_ROLE_CLIENT;
    psa_status_t status = psa_pake_set_role(&mOperation, PSA_PAKE_ROLE_CLIENT);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_set_user(&mOperation, my_identity, my_identity_len);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_set_peer(&mOperation, peer_identity, peer_identity_len);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    uint8_t password[kSpake2p_WS_Length * 2];
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;

    memcpy(password + 0, w0in, w0in_len);
    memcpy(password + w0in_len, w1in, w1in_len);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_SPAKE2P);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_PASSWORD);

    status = psa_import_key(&attributes, password, w0in_len + w1in_len, &mKey);
    psa_reset_key_attributes(&attributes);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_set_password_key(&mOperation, mKey);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_input(&mOperation, PSA_PAKE_STEP_CONTEXT, mContext, mContextLen);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    return CHIP_NO_ERROR;
}

CHIP_ERROR PSASpake2p_P256_SHA256_HKDF_HMAC::ComputeRoundOne(const uint8_t * pab, size_t pab_len, uint8_t * out, size_t * out_len)
{
    VerifyOrReturnError(out_len != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    psa_status_t status;

    if (mRole == PSA_PAKE_ROLE_SERVER)
    {
        status = psa_pake_input(&mOperation, PSA_PAKE_STEP_KEY_SHARE, pab, pab_len);
        VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);
    }

    status = psa_pake_output(&mOperation, PSA_PAKE_STEP_KEY_SHARE, out, *out_len, out_len);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    return CHIP_NO_ERROR;
}

CHIP_ERROR PSASpake2p_P256_SHA256_HKDF_HMAC::ComputeRoundTwo(const uint8_t * in, size_t in_len, uint8_t * out, size_t * out_len)
{
    VerifyOrReturnError(out_len != nullptr, CHIP_ERROR_INVALID_ARGUMENT);

    psa_status_t status;

    if (mRole == PSA_PAKE_ROLE_CLIENT)
    {
        status = psa_pake_input(&mOperation, PSA_PAKE_STEP_KEY_SHARE, in, in_len);
        VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);
    }

    status = psa_pake_output(&mOperation, PSA_PAKE_STEP_CONFIRM, out, *out_len, out_len);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    return CHIP_NO_ERROR;
}

CHIP_ERROR PSASpake2p_P256_SHA256_HKDF_HMAC::KeyConfirm(const uint8_t * in, size_t in_len)
{
    psa_status_t status = psa_pake_input(&mOperation, PSA_PAKE_STEP_CONFIRM, in, in_len);
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    return CHIP_NO_ERROR;
}

CHIP_ERROR PSASpake2p_P256_SHA256_HKDF_HMAC::GetKeys(SessionKeystore & keystore, HkdfKeyHandle & key)
{
    /*
     * TODO: use psa_pake_shared_secret() proposed in https://github.com/ARM-software/psa-api/issues/86
     */

    psa_key_derivation_operation_t * kdf = Platform::New<psa_key_derivation_operation_t>();
    Platform::UniquePtr<psa_key_derivation_operation_t> kdfPtr(kdf);

    VerifyOrReturnError(kdfPtr, CHIP_ERROR_NO_MEMORY);

    *kdfPtr = PSA_KEY_DERIVATION_OPERATION_INIT;

    psa_status_t status = psa_key_derivation_setup(kdfPtr.get(), PSA_ALG_HKDF(PSA_ALG_SHA_256));
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    status = psa_pake_get_implicit_key(&mOperation, kdfPtr.get());
    VerifyOrReturnError(status == PSA_SUCCESS, CHIP_ERROR_INTERNAL);

    auto & hkdfKeyHandle           = key.AsMutable<PsaHkdfKeyHandle>();
    hkdfKeyHandle.mKeyDerivationOp = kdfPtr.get();
    hkdfKeyHandle.mIsKeyId         = false;

    kdfPtr.release();

    return CHIP_NO_ERROR;
}

} // namespace Crypto
} // namespace chip
