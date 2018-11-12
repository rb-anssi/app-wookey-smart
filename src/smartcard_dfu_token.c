#include "smartcard_dfu_token.h"
#include "api/syscall.h"
#define SMARTCARD_DEBUG
#define MEASURE_TOKEN_PERF

#include "api/print.h"

/* Include our encrypted platform keys  */
#include "DFU/encrypted_platform_dfu_keys.h"

#define SMARTCARD_DEBUG

/* Primitive for debug output */
#ifdef SMARTCARD_DEBUG
#define log_printf(...) printf(__VA_ARGS__)
#else
#define log_printf(...)
#endif


static const unsigned char dfu_applet_AID[] = { 0x45, 0x75, 0x74, 0x77, 0x74, 0x75, 0x36, 0x41, 0x70, 0x71 };

/* Ask the DFU token to begin a decrypt session by sending the initial IV and its HMAC */
int dfu_token_begin_decrypt_session(token_channel *channel, const unsigned char *iv, uint32_t iv_len, const unsigned char *iv_hmac, uint32_t iv_hmac_len){
        SC_APDU_cmd apdu;
        SC_APDU_resp resp;

	/* Sanity checks */
	if((channel == NULL) || (iv == NULL) || (iv_hmac == NULL)){
		goto err;
	}
	/* Sanity checks on the sizes */
	if((iv_len != AES_BLOCK_SIZE) || (iv_hmac_len != SHA256_DIGEST_SIZE)){
		goto err;
	}

        apdu.cla = 0x00; apdu.ins = TOKEN_INS_BEGIN_DECRYPT_SESSION; apdu.p1 = 0x00; apdu.p2 = 0x00; apdu.lc = AES_BLOCK_SIZE+SHA256_DIGEST_SIZE; apdu.le = 0; apdu.send_le = 1;
	memcpy(apdu.data, iv, iv_len);
	memcpy(apdu.data+iv_len, iv_hmac, iv_hmac_len);
        if(token_send_receive(channel, &apdu, &resp)){
                goto err;
        }

        /******* Token response ***********************************/
        if((resp.sw1 != (TOKEN_RESP_OK >> 8)) || (resp.sw2 != (TOKEN_RESP_OK & 0xff))){
                /* The smartcard responded an error */
                goto err;
        }
	
        return 0;
err:
        return -1;
}

/* Ask the DFU token to derive a session key for a firmware chunk */
int dfu_token_derive_key(token_channel *channel, unsigned char *derived_key, uint32_t derived_key_len){
        SC_APDU_cmd apdu;
        SC_APDU_resp resp;

        /* Sanity checks */
        if((channel == NULL) || (derived_key == NULL)){
                goto err;
        }
	if(derived_key_len < AES_BLOCK_SIZE){
		goto err;
	}
	
        apdu.cla = 0x00; apdu.ins = TOKEN_INS_DERIVE_KEY; apdu.p1 = 0x00; apdu.p2 = 0x00; apdu.lc = 0; apdu.le = AES_BLOCK_SIZE; apdu.send_le = 1;
        if(token_send_receive(channel, &apdu, &resp)){
                goto err;
        }

        /******* Token response ***********************************/
        if((resp.sw1 != (TOKEN_RESP_OK >> 8)) || (resp.sw2 != (TOKEN_RESP_OK & 0xff))){
                /* The smartcard responded an error */
                goto err;
        }
	/* Check the response */
	if(resp.le != AES_BLOCK_SIZE){
		goto err;
	}
	memcpy(derived_key, resp.data, AES_BLOCK_SIZE);
	
        return 0;
err:
        return -1;
}

int dfu_token_unlock_ops_exec(token_channel *channel, token_unlock_operations *ops, uint32_t num_ops, cb_token_callbacks *callbacks, unsigned char *decrypted_sig_pub_key_data, unsigned int *decrypted_sig_pub_key_data_len){
        if(token_unlock_ops_exec(channel, dfu_applet_AID, sizeof(dfu_applet_AID), keybag_dfu, sizeof(keybag_dfu)/sizeof(databag), PLATFORM_PBKDF2_ITERATIONS, USED_SIGNATURE_CURVE, ops, num_ops, callbacks, decrypted_sig_pub_key_data, decrypted_sig_pub_key_data_len)){
                goto err;
        }

        return 0;
err:
        return -1;
}

int dfu_token_exchanges(token_channel *channel, cb_token_callbacks *callbacks, unsigned char *decrypted_sig_pub_key_data, unsigned int *decrypted_sig_pub_key_data_len){
        /* Sanity checks */
        if(channel == NULL){
                goto err;
        }	
	if(callbacks == NULL){
		goto err;
	}

        token_unlock_operations ops[] = { TOKEN_UNLOCK_INIT_TOKEN, TOKEN_UNLOCK_ASK_PET_PIN, TOKEN_UNLOCK_PRESENT_PET_PIN, TOKEN_UNLOCK_ESTABLISH_SECURE_CHANNEL, TOKEN_UNLOCK_CONFIRM_PET_NAME, TOKEN_UNLOCK_PRESENT_USER_PIN };
        if(dfu_token_unlock_ops_exec(channel, ops, sizeof(ops)/sizeof(token_unlock_operations), callbacks, decrypted_sig_pub_key_data, decrypted_sig_pub_key_data_len)){
                goto err;
        }

	unsigned char iv[] = "\xb9\xec\x9d\xce\x21\xf9\x3a\x68\xc5\x47\x0e\x2a\x52\xc8\x9b\x9e";
	unsigned char iv_hmac[] = "\xbb\x31\x52\x34\xdb\x3f\x87\x82\x9c\x93\xc6\x56\x79\x71\x4d\x2c\xe1\x52\xd1\xa6\x71\xc4\x50\x98\x43\x7f\x7c\xba\x67\xeb\xe5\x3f";
	if(dfu_token_begin_decrypt_session(channel, iv, 16, iv_hmac, 32)){
		goto err;
	}

	return 0;

err:
        /* Zeroize token channel */
        token_zeroize_channel(channel);

	return -1;
}
