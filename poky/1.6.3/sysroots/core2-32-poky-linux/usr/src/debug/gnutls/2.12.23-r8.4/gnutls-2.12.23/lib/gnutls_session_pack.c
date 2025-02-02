/*
 * Copyright (C) 2000, 2004, 2005, 2007, 2008, 2009, 2010 Free Software
 * Foundation, Inc.
 *
 * Author: Nikos Mavrogiannopoulos
 *
 * This file is part of GnuTLS.
 *
 * The GnuTLS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA
 *
 */

/* Contains functions that are supposed to pack and unpack session data,
 * before and after they are sent to the database backend.
 */

#include <gnutls_int.h>
#ifdef ENABLE_SRP
#include <auth_srp.h>
#endif
#ifdef ENABLE_PSK
#include <auth_psk.h>
#endif
#include <auth_anon.h>
#include <auth_cert.h>
#include <gnutls_errors.h>
#include <gnutls_auth.h>
#include <gnutls_session_pack.h>
#include <gnutls_datum.h>
#include <gnutls_num.h>
#include <gnutls_extensions.h>
#include <gnutls_constate.h>

static int pack_certificate_auth_info (gnutls_session_t,
                                       gnutls_buffer_st * packed_session);
static int unpack_certificate_auth_info (gnutls_session_t,
                                         gnutls_buffer_st * packed_session);

static int unpack_srp_auth_info (gnutls_session_t session,
                                 gnutls_buffer_st * packed_session);
static int pack_srp_auth_info (gnutls_session_t session,
                               gnutls_buffer_st * packed_session);

static int unpack_psk_auth_info (gnutls_session_t session,
                                 gnutls_buffer_st * packed_session);
static int pack_psk_auth_info (gnutls_session_t session,
                               gnutls_buffer_st * packed_session);

static int unpack_anon_auth_info (gnutls_session_t session,
                                  gnutls_buffer_st * packed_session);
static int pack_anon_auth_info (gnutls_session_t session,
                                gnutls_buffer_st * packed_session);

static int unpack_security_parameters (gnutls_session_t session,
                                       gnutls_buffer_st * packed_session);
static int pack_security_parameters (gnutls_session_t session,
                                     gnutls_buffer_st * packed_session);


/* Since auth_info structures contain malloced data, this function
 * is required in order to pack these structures in a vector in
 * order to store them to the DB.
 *
 * packed_session will contain the session data.
 *
 * The data will be in a platform independent format.
 */
int
_gnutls_session_pack (gnutls_session_t session,
                      gnutls_datum_t * packed_session)
{
  int ret;
  gnutls_buffer_st sb;
  opaque id;

  if (packed_session == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }

  _gnutls_buffer_init (&sb);

  id = gnutls_auth_get_type (session);
  BUFFER_APPEND (&sb, &id, 1);

  switch (id)
    {
#ifdef ENABLE_SRP
    case GNUTLS_CRD_SRP:
      ret = pack_srp_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          return ret;
        }
      break;
#endif
#ifdef ENABLE_PSK
    case GNUTLS_CRD_PSK:
      ret = pack_psk_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          return ret;
        }
      break;
#endif
#ifdef ENABLE_ANON
    case GNUTLS_CRD_ANON:
      ret = pack_anon_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          return ret;
        }
      break;
#endif
    case GNUTLS_CRD_CERTIFICATE:
      ret = pack_certificate_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          return ret;
        }
      break;
    default:
      return GNUTLS_E_INTERNAL_ERROR;

    }

  /* Auth_info structures copied. Now copy security_parameters_st. 
   * packed_session must have allocated space for the security parameters.
   */
  ret = pack_security_parameters (session, &sb);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_buffer_clear (&sb);
      return ret;
    }

  ret = _gnutls_ext_pack (session, &sb);
  if (ret < 0)
    {
      gnutls_assert ();
      _gnutls_buffer_clear (&sb);
      return ret;
    }

  ret = _gnutls_buffer_to_datum (&sb, packed_session);

  return ret;
}


/* Load session data from a buffer.
 */
int
_gnutls_session_unpack (gnutls_session_t session,
                        const gnutls_datum_t * packed_session)
{
  int ret;
  gnutls_buffer_st sb;
  opaque id;

  _gnutls_buffer_init (&sb);

  if (packed_session == NULL || packed_session->size == 0)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }

  ret =
    _gnutls_buffer_append_data (&sb, packed_session->data,
                                packed_session->size);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  if (_gnutls_get_auth_info (session) != NULL)
    {
      _gnutls_free_auth_info (session);
    }

  BUFFER_POP (&sb, &id, 1);

  switch (id)
    {
#ifdef ENABLE_SRP
    case GNUTLS_CRD_SRP:
      ret = unpack_srp_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          goto error;
        }
      break;
#endif
#ifdef ENABLE_PSK
    case GNUTLS_CRD_PSK:
      ret = unpack_psk_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          goto error;
        }
      break;
#endif
#ifdef ENABLE_ANON
    case GNUTLS_CRD_ANON:
      ret = unpack_anon_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          return ret;
        }
      break;
#endif
    case GNUTLS_CRD_CERTIFICATE:
      ret = unpack_certificate_auth_info (session, &sb);
      if (ret < 0)
        {
          gnutls_assert ();
          goto error;
        }
      break;
    default:
      gnutls_assert ();
      ret = GNUTLS_E_INTERNAL_ERROR;
      goto error;

    }

  /* Auth_info structures copied. Now copy security_parameters_st. 
   * packed_session must have allocated space for the security parameters.
   */
  ret = unpack_security_parameters (session, &sb);
  if (ret < 0)
    {
      gnutls_assert ();
      goto error;
    }

  ret = _gnutls_ext_unpack (session, &sb);
  if (ret < 0)
    {
      gnutls_assert ();
      goto error;
    }

  ret = 0;

error:
  _gnutls_buffer_clear (&sb);

  return ret;
}



/* Format: 
 *      1 byte the credentials type
 *      4 bytes the size of the whole structure
 *        DH stuff
 *      2 bytes the size of secret key in bits
 *      4 bytes the size of the prime
 *      x bytes the prime
 *      4 bytes the size of the generator
 *      x bytes the generator
 *      4 bytes the size of the public key
 *      x bytes the public key
 *        RSA stuff
 *      4 bytes the size of the modulus
 *      x bytes the modulus
 *      4 bytes the size of the exponent
 *      x bytes the exponent
 *        CERTIFICATES
 *      4 bytes the length of the certificate list
 *      4 bytes the size of first certificate
 *      x bytes the certificate
 *       and so on...
 */
static int
pack_certificate_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  unsigned int i;
  int cur_size, ret;
  cert_auth_info_t info = _gnutls_get_auth_info (session);
  int size_offset;

  size_offset = ps->length;
  BUFFER_APPEND_NUM (ps, 0);
  cur_size = ps->length;

  if (info)
    {

      BUFFER_APPEND_NUM (ps, info->dh.secret_bits);
      BUFFER_APPEND_PFX (ps, info->dh.prime.data, info->dh.prime.size);
      BUFFER_APPEND_PFX (ps, info->dh.generator.data,
                         info->dh.generator.size);
      BUFFER_APPEND_PFX (ps, info->dh.public_key.data,
                         info->dh.public_key.size);
      BUFFER_APPEND_PFX (ps, info->rsa_export.modulus.data,
                         info->rsa_export.modulus.size);
      BUFFER_APPEND_PFX (ps, info->rsa_export.exponent.data,
                         info->rsa_export.exponent.size);

      BUFFER_APPEND_NUM (ps, info->ncerts);

      for (i = 0; i < info->ncerts; i++)
        BUFFER_APPEND_PFX (ps, info->raw_certificate_list[i].data,
                           info->raw_certificate_list[i].size);
    }

  /* write the real size */
  _gnutls_write_uint32 (ps->length - cur_size, ps->data + size_offset);

  return 0;
}


/* Upack certificate info.
 */
static int
unpack_certificate_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  int ret;
  unsigned int i = 0, j = 0;
  size_t pack_size;
  cert_auth_info_t info = NULL;

  BUFFER_POP_NUM (ps, pack_size);

  if (pack_size == 0)
    return 0;                   /* nothing to be done */

  /* client and server have the same auth_info here
   */
  ret =
    _gnutls_auth_info_set (session, GNUTLS_CRD_CERTIFICATE,
                           sizeof (cert_auth_info_st), 1);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  info = _gnutls_get_auth_info (session);
  if (info == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }

  BUFFER_POP_NUM (ps, info->dh.secret_bits);

  BUFFER_POP_DATUM (ps, &info->dh.prime);
  BUFFER_POP_DATUM (ps, &info->dh.generator);
  BUFFER_POP_DATUM (ps, &info->dh.public_key);
  BUFFER_POP_DATUM (ps, &info->rsa_export.modulus);
  BUFFER_POP_DATUM (ps, &info->rsa_export.exponent);

  BUFFER_POP_NUM (ps, info->ncerts);

  if (info->ncerts > 0)
    {
      info->raw_certificate_list =
        gnutls_calloc (info->ncerts, sizeof (gnutls_datum_t));
      if (info->raw_certificate_list == NULL)
        {
          gnutls_assert ();
          ret = GNUTLS_E_MEMORY_ERROR;
          goto error;
        }
    }

  for (i = 0; i < info->ncerts; i++)
    {
      BUFFER_POP_DATUM (ps, &info->raw_certificate_list[i]);
    }

  return 0;

error:
  if (info)
    {
      _gnutls_free_datum (&info->dh.prime);
      _gnutls_free_datum (&info->dh.generator);
      _gnutls_free_datum (&info->dh.public_key);

      _gnutls_free_datum (&info->rsa_export.modulus);
      _gnutls_free_datum (&info->rsa_export.exponent);

      for (j = 0; j < i; j++)
        _gnutls_free_datum (&info->raw_certificate_list[j]);

      gnutls_free (info->raw_certificate_list);
    }

  return ret;

}

#ifdef ENABLE_SRP
/* Packs the SRP session authentication data.
 */

/* Format: 
 *      1 byte the credentials type
 *      4 bytes the size of the SRP username (x)
 *      x bytes the SRP username
 */
static int
pack_srp_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  srp_server_auth_info_t info = _gnutls_get_auth_info (session);
  int len, ret;
  int size_offset;
  size_t cur_size;

  if (info && info->username)
    len = strlen (info->username) + 1;  /* include the terminating null */
  else
    len = 0;

  size_offset = ps->length;
  BUFFER_APPEND_NUM (ps, 0);
  cur_size = ps->length;

  BUFFER_APPEND_PFX (ps, info->username, len);

  /* write the real size */
  _gnutls_write_uint32 (ps->length - cur_size, ps->data + size_offset);

  return 0;
}


static int
unpack_srp_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  size_t username_size;
  int ret;
  srp_server_auth_info_t info;

  BUFFER_POP_NUM (ps, username_size);
  if (username_size > sizeof (info->username))
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }


  ret =
    _gnutls_auth_info_set (session, GNUTLS_CRD_SRP,
                           sizeof (srp_server_auth_info_st), 1);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  info = _gnutls_get_auth_info (session);
  if (info == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }

  BUFFER_POP (ps, info->username, username_size);

  ret = 0;

error:
  return ret;
}
#endif


#ifdef ENABLE_ANON
/* Packs the ANON session authentication data.
 */

/* Format: 
 *      1 byte the credentials type
 *      4 bytes the size of the whole structure
 *      2 bytes the size of secret key in bits
 *      4 bytes the size of the prime
 *      x bytes the prime
 *      4 bytes the size of the generator
 *      x bytes the generator
 *      4 bytes the size of the public key
 *      x bytes the public key
 */
static int
pack_anon_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  int cur_size, ret;
  anon_auth_info_t info = _gnutls_get_auth_info (session);
  int size_offset;

  size_offset = ps->length;
  BUFFER_APPEND_NUM (ps, 0);
  cur_size = ps->length;

  if (info)
    {
      BUFFER_APPEND_NUM (ps, info->dh.secret_bits);
      BUFFER_APPEND_PFX (ps, info->dh.prime.data, info->dh.prime.size);
      BUFFER_APPEND_PFX (ps, info->dh.generator.data,
                         info->dh.generator.size);
      BUFFER_APPEND_PFX (ps, info->dh.public_key.data,
                         info->dh.public_key.size);
    }

  /* write the real size */
  _gnutls_write_uint32 (ps->length - cur_size, ps->data + size_offset);

  return 0;
}


static int
unpack_anon_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  int ret;
  size_t pack_size;
  anon_auth_info_t info = NULL;

  BUFFER_POP_NUM (ps, pack_size);

  if (pack_size == 0)
    return 0;                   /* nothing to be done */

  /* client and server have the same auth_info here
   */
  ret =
    _gnutls_auth_info_set (session, GNUTLS_CRD_ANON,
                           sizeof (anon_auth_info_st), 1);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  info = _gnutls_get_auth_info (session);
  if (info == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }

  BUFFER_POP_NUM (ps, info->dh.secret_bits);

  BUFFER_POP_DATUM (ps, &info->dh.prime);
  BUFFER_POP_DATUM (ps, &info->dh.generator);
  BUFFER_POP_DATUM (ps, &info->dh.public_key);

  return 0;

error:
  if (info)
    {
      _gnutls_free_datum (&info->dh.prime);
      _gnutls_free_datum (&info->dh.generator);
      _gnutls_free_datum (&info->dh.public_key);
    }

  return ret;
}
#endif /* ANON */

#ifdef ENABLE_PSK
/* Packs the PSK session authentication data.
 */

/* Format: 
 *      1 byte the credentials type
 *      4 bytes the size of the whole structure
 *
 *      4 bytes the size of the PSK username (x)
 *      x bytes the PSK username
 *      2 bytes the size of secret key in bits
 *      4 bytes the size of the prime
 *      x bytes the prime
 *      4 bytes the size of the generator
 *      x bytes the generator
 *      4 bytes the size of the public key
 *      x bytes the public key
 */
static int
pack_psk_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  psk_auth_info_t info;
  int username_len;
  int hint_len, ret;
  int size_offset;
  size_t cur_size;

  info = _gnutls_get_auth_info (session);

  if (info && info->username)
    username_len = strlen (info->username) + 1; /* include the terminating null */
  else
    username_len = 0;

  if (info && info->hint)
    hint_len = strlen (info->hint) + 1; /* include the terminating null */
  else
    hint_len = 0;

  size_offset = ps->length;
  BUFFER_APPEND_NUM (ps, 0);
  cur_size = ps->length;

  BUFFER_APPEND_PFX (ps, info->username, username_len);
  BUFFER_APPEND_PFX (ps, info->hint, hint_len);

  BUFFER_APPEND_NUM (ps, info->dh.secret_bits);
  BUFFER_APPEND_PFX (ps, info->dh.prime.data, info->dh.prime.size);
  BUFFER_APPEND_PFX (ps, info->dh.generator.data, info->dh.generator.size);
  BUFFER_APPEND_PFX (ps, info->dh.public_key.data, info->dh.public_key.size);

  /* write the real size */
  _gnutls_write_uint32 (ps->length - cur_size, ps->data + size_offset);

  return 0;
}

static int
unpack_psk_auth_info (gnutls_session_t session, gnutls_buffer_st * ps)
{
  size_t username_size, hint_size;
  int ret;
  psk_auth_info_t info;

  ret =
    _gnutls_auth_info_set (session, GNUTLS_CRD_PSK,
                           sizeof (psk_auth_info_st), 1);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  info = _gnutls_get_auth_info (session);
  if (info == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }

  BUFFER_POP_NUM (ps, username_size);
  if (username_size > sizeof (info->username))
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }

  BUFFER_POP (ps, info->username, username_size);

  BUFFER_POP_NUM (ps, hint_size);
  if (hint_size > sizeof (info->hint))
    {
      gnutls_assert ();
      return GNUTLS_E_INTERNAL_ERROR;
    }
  BUFFER_POP (ps, info->hint, hint_size);

  BUFFER_POP_NUM (ps, info->dh.secret_bits);

  BUFFER_POP_DATUM (ps, &info->dh.prime);
  BUFFER_POP_DATUM (ps, &info->dh.generator);
  BUFFER_POP_DATUM (ps, &info->dh.public_key);

  ret = 0;

error:
  _gnutls_free_datum (&info->dh.prime);
  _gnutls_free_datum (&info->dh.generator);
  _gnutls_free_datum (&info->dh.public_key);

  return ret;
}
#endif


/* Packs the security parameters.
 */

/* Format: 
 *      4 bytes the total security data size
 *      1 byte the entity type (client/server)
 *      1 byte the key exchange algorithm used
 *      1 byte the read cipher algorithm
 *      1 byte the read mac algorithm
 *      1 byte the read compression algorithm
 *
 *      1 byte the write cipher algorithm
 *      1 byte the write mac algorithm
 *      1 byte the write compression algorithm
 *
 *      1 byte the certificate type
 *      1 byte the protocol version
 *
 *      2 bytes the cipher suite
 *
 *      48 bytes the master secret
 *
 *      32 bytes the client random
 *      32 bytes the server random
 *
 *      1 byte the session ID size
 *      x bytes the session ID (32 bytes max)
 *
 *      4 bytes a timestamp
 *            -------------------
 *                MAX: 165 bytes
 *
 */
static int
pack_security_parameters (gnutls_session_t session, gnutls_buffer_st * ps)
{

  int ret;
  int size_offset;
  size_t cur_size;
  record_parameters_st *params;

  if (session->security_parameters.epoch_read
      != session->security_parameters.epoch_write)
    {
      gnutls_assert ();
      return GNUTLS_E_INVALID_REQUEST;
    }

  ret = _gnutls_epoch_get (session, EPOCH_READ_CURRENT, &params);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }

  /* move after the auth info stuff.
   */
  size_offset = ps->length;
  BUFFER_APPEND_NUM (ps, 0);
  cur_size = ps->length;


  BUFFER_APPEND_NUM (ps, session->security_parameters.entity);
  BUFFER_APPEND_NUM (ps, session->security_parameters.kx_algorithm);
  BUFFER_APPEND (ps,
                 &session->security_parameters.current_cipher_suite.suite[0],
                 1);
  BUFFER_APPEND (ps,
                 &session->security_parameters.current_cipher_suite.suite[1],
                 1);
  BUFFER_APPEND_NUM (ps, params->compression_algorithm);
  BUFFER_APPEND_NUM (ps, session->security_parameters.cert_type);
  BUFFER_APPEND_NUM (ps, session->security_parameters.version);

  BUFFER_APPEND (ps, session->security_parameters.master_secret,
                 GNUTLS_MASTER_SIZE);
  BUFFER_APPEND (ps, session->security_parameters.client_random,
                 GNUTLS_RANDOM_SIZE);
  BUFFER_APPEND (ps, session->security_parameters.server_random,
                 GNUTLS_RANDOM_SIZE);

  BUFFER_APPEND_NUM (ps, session->security_parameters.session_id_size);
  BUFFER_APPEND (ps, session->security_parameters.session_id,
                 session->security_parameters.session_id_size);

  BUFFER_APPEND_NUM (ps, session->security_parameters.max_record_send_size);
  BUFFER_APPEND_NUM (ps, session->security_parameters.max_record_recv_size);
  BUFFER_APPEND_NUM (ps, session->security_parameters.timestamp);

  _gnutls_write_uint32 (ps->length - cur_size, ps->data + size_offset);

  return 0;
}

static int
unpack_security_parameters (gnutls_session_t session, gnutls_buffer_st * ps)
{
  size_t pack_size;
  int ret;
  time_t timestamp = gnutls_time (0);

  BUFFER_POP_NUM (ps, pack_size);

  if (pack_size == 0)
    return GNUTLS_E_INVALID_REQUEST;

  memset (&session->internals.resumed_security_parameters, 0,
          sizeof (session->internals.resumed_security_parameters));

  BUFFER_POP_NUM (ps, session->internals.resumed_security_parameters.entity);
  BUFFER_POP_NUM (ps,
              session->internals.resumed_security_parameters.kx_algorithm);
  BUFFER_POP (ps,
              &session->internals.
              resumed_security_parameters.current_cipher_suite.suite[0], 1);
  BUFFER_POP (ps,
              &session->internals.resumed_security_parameters.
              current_cipher_suite.suite[1], 1);
  BUFFER_POP_NUM (ps, session->internals.resumed_compression_method);
  BUFFER_POP_NUM (ps, session->internals.resumed_security_parameters.cert_type);
  BUFFER_POP_NUM (ps, session->internals.resumed_security_parameters.version);

  BUFFER_POP (ps,
              session->internals.resumed_security_parameters.master_secret,
              GNUTLS_MASTER_SIZE);

  BUFFER_POP (ps,
              session->internals.resumed_security_parameters.client_random,
              GNUTLS_RANDOM_SIZE);
  BUFFER_POP (ps,
              session->internals.resumed_security_parameters.server_random,
              GNUTLS_RANDOM_SIZE);
  BUFFER_POP_NUM (ps,
                  session->internals.
                  resumed_security_parameters.session_id_size);

  BUFFER_POP (ps, session->internals.resumed_security_parameters.session_id,
              session->internals.resumed_security_parameters.session_id_size);

  BUFFER_POP_NUM (ps,
                  session->internals.
                  resumed_security_parameters.max_record_send_size);
  BUFFER_POP_NUM (ps,
                  session->internals.
                  resumed_security_parameters.max_record_recv_size);
  BUFFER_POP_NUM (ps,
                  session->internals.resumed_security_parameters.timestamp);

  if (timestamp - session->internals.resumed_security_parameters.timestamp >
      session->internals.expire_time
      || session->internals.resumed_security_parameters.timestamp > timestamp)
    {
      gnutls_assert ();
      return GNUTLS_E_EXPIRED;
    }

  ret = 0;

error:
  return ret;
}
