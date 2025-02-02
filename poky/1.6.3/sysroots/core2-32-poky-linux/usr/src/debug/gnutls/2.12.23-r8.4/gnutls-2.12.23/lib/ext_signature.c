/*
 * Copyright (C) 2002,2003,2004,2005,2009,2010,2011 Free Software
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

/* This file contains the code the Certificate Type TLS extension.
 * This extension is currently gnutls specific.
 */

#include "gnutls_int.h"
#include "gnutls_errors.h"
#include "gnutls_num.h"
#include <ext_signature.h>
#include <gnutls_state.h>
#include <gnutls_num.h>
#include <gnutls_algorithms.h>
#include <x509/common.h> /* dsa_q_to_hash */
#include <gnutls_cert.h>

static int _gnutls_signature_algorithm_recv_params (gnutls_session_t session,
                                                    const opaque * data,
                                                    size_t data_size);
static int _gnutls_signature_algorithm_send_params (gnutls_session_t session,
                                                    opaque * data, size_t);
static void signature_algorithms_deinit_data (extension_priv_data_t priv);
static int signature_algorithms_pack (extension_priv_data_t epriv,
                                      gnutls_buffer_st * ps);
static int signature_algorithms_unpack (gnutls_buffer_st * ps,
                                        extension_priv_data_t * _priv);

extension_entry_st ext_mod_sig = {
  .name = "SIGNATURE ALGORITHMS",
  .type = GNUTLS_EXTENSION_SIGNATURE_ALGORITHMS,
  .parse_type = GNUTLS_EXT_TLS,

  .recv_func = _gnutls_signature_algorithm_recv_params,
  .send_func = _gnutls_signature_algorithm_send_params,
  .pack_func = signature_algorithms_pack,
  .unpack_func = signature_algorithms_unpack,
  .deinit_func = signature_algorithms_deinit_data,
};

typedef struct
{
  /* TLS 1.2 signature algorithms */
  gnutls_sign_algorithm_t sign_algorithms[MAX_SIGNATURE_ALGORITHMS];
  uint16_t sign_algorithms_size;
} sig_ext_st;

/* generates a SignatureAndHashAlgorithm structure with length as prefix
 * by using the setup priorities.
 */
int
_gnutls_sign_algorithm_write_params (gnutls_session_t session, opaque * data,
                                     size_t max_data_size)
{
  opaque *p = data, *len_p;
  int len, i, j, hash;
  const sign_algorithm_st *aid;

  if (max_data_size < (session->internals.priorities.sign_algo.algorithms*2) + 2)
    {
      gnutls_assert ();
      return GNUTLS_E_SHORT_MEMORY_BUFFER;
    }

  len = 0;
  len_p = p;

  p += 2;

  for (i = j = 0; j < session->internals.priorities.sign_algo.algorithms; i += 2, j++)
    {
      /* In gnutls we keep a state of SHA1 and SHA256 and thus cannot
       * use anything else.
       */
      hash = _gnutls_sign_get_hash_algorithm(session->internals.priorities.sign_algo.priority[j]);
      if (hash != GNUTLS_DIG_SHA1 && hash != GNUTLS_DIG_SHA256)
        continue;
      
      aid =
        _gnutls_sign_to_tls_aid (session->internals.priorities.
                                 sign_algo.priority[j]);

      if (aid == NULL)
        continue;
        
       _gnutls_debug_log ("EXT[SIGA]: sent signature algo (%d.%d) %s\n", aid->hash_algorithm, 
         aid->sign_algorithm, gnutls_sign_get_name(session->internals.priorities.sign_algo.priority[j]));
      *p = aid->hash_algorithm;
      p++;
      *p = aid->sign_algorithm;
      p++;
      len+=2;
    }

  _gnutls_write_uint16 (len, len_p);

  return len + 2;
}


/* Parses the Signature Algorithm structure and stores data into
 * session->security_parameters.extensions.
 */
int
_gnutls_sign_algorithm_parse_data (gnutls_session_t session,
                                   const opaque * data, size_t data_size)
{
  int sig, i, hash;
  sig_ext_st *priv;
  extension_priv_data_t epriv;

  priv = gnutls_calloc (1, sizeof (*priv));
  if (priv == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  for (i = 0; i < data_size; i += 2)
    {
      sign_algorithm_st aid;

      aid.hash_algorithm = data[i];
      aid.sign_algorithm = data[i + 1];

      sig = _gnutls_tls_aid_to_sign (&aid);

       _gnutls_debug_log ("EXT[SIGA]: rcvd signature algo (%d.%d) %s\n", aid.hash_algorithm, 
         aid.sign_algorithm, gnutls_sign_get_name(sig));


      if (sig != GNUTLS_SIGN_UNKNOWN)
        {
          hash = _gnutls_sign_get_hash_algorithm(sig);
          if (hash != GNUTLS_DIG_SHA1 && hash != GNUTLS_DIG_SHA256)
            continue;

          priv->sign_algorithms[priv->sign_algorithms_size++] = sig;
          if (priv->sign_algorithms_size == MAX_SIGNATURE_ALGORITHMS)
            break;
        }
    }

  epriv.ptr = priv;
  _gnutls_ext_set_session_data (session,
                                GNUTLS_EXTENSION_SIGNATURE_ALGORITHMS, epriv);

  return 0;
}

/*
 * In case of a server: if a SIGNATURE_ALGORITHMS extension type is
 * received then it stores into the session security parameters the
 * new value.
 *
 * In case of a client: If a signature_algorithms have been specified
 * then it is an error;
 */

static int
_gnutls_signature_algorithm_recv_params (gnutls_session_t session,
                                         const opaque * data,
                                         size_t _data_size)
{
  ssize_t data_size = _data_size;
  int ret;

  if (session->security_parameters.entity == GNUTLS_CLIENT)
    {
      /* nothing for now */
      gnutls_assert ();
      /* Although TLS 1.2 mandates that we must not accept reply
       * to this message, there are good reasons to just ignore it. Check
       * http://www.ietf.org/mail-archive/web/tls/current/msg03880.html
       */
      /* return GNUTLS_E_UNEXPECTED_PACKET; */
    }
  else
    {
      /* SERVER SIDE - we must check if the sent cert type is the right one
       */
      if (data_size > 2)
        {
          uint16_t len;

          DECR_LEN (data_size, 2);
          len = _gnutls_read_uint16 (data);
          DECR_LEN (data_size, len);

          ret = _gnutls_sign_algorithm_parse_data (session, data + 2, len);
          if (ret < 0)
            {
              gnutls_assert ();
              return ret;
            }
        }
    }

  return 0;
}

/* returns data_size or a negative number on failure
 */
static int
_gnutls_signature_algorithm_send_params (gnutls_session_t session,
                                         opaque * data, size_t data_size)
{
  int ret;
  gnutls_protocol_t ver = gnutls_protocol_get_version (session);

  /* this function sends the client extension data */
  if (session->security_parameters.entity == GNUTLS_CLIENT
      && _gnutls_version_has_selectable_sighash (ver))
    {
      if (session->internals.priorities.sign_algo.algorithms > 0)
        {
          ret =
            _gnutls_sign_algorithm_write_params (session, data, data_size);
          if (ret < 0)
            {
              gnutls_assert ();
              return ret;
            }
          return ret;
        }
    }

  /* if we are here it means we don't send the extension */
  return 0;
}

int cert_compatible_with_sig(gnutls_cert* cert, gnutls_protocol_t ver, 
  gnutls_sign_algorithm_t sign)
{
unsigned int hash_len;

  if (cert->subject_pk_algorithm == GNUTLS_PK_DSA)
    { /* override */
      int hash_algo = _gnutls_dsa_q_to_hash (cert->params[1], &hash_len);

      /* DSA keys over 1024 bits cannot be used with TLS 1.x, x<2 */
      if (!_gnutls_version_has_selectable_sighash (ver))
        {
          if (hash_algo != GNUTLS_DIG_SHA1)
            return gnutls_assert_val(GNUTLS_E_INCOMPAT_DSA_KEY_WITH_TLS_PROTOCOL);
        }
      else
        {
          if (_gnutls_hash_get_algo_len(_gnutls_sign_get_hash_algorithm(sign)) < hash_len)
            return GNUTLS_E_UNWANTED_ALGORITHM;
        }
        
    }

  return 0;
}

/* Returns a requested by the peer signature algorithm that
 * matches the given public key algorithm. Index can be increased
 * to return the second choice etc.
 */
gnutls_sign_algorithm_t
_gnutls_session_get_sign_algo (gnutls_session_t session, gnutls_cert* cert)
{
  unsigned i;
  int ret;
  gnutls_protocol_t ver = gnutls_protocol_get_version (session);
  sig_ext_st *priv;
  extension_priv_data_t epriv;

  ret =
    _gnutls_ext_get_session_data (session,
                                  GNUTLS_EXTENSION_SIGNATURE_ALGORITHMS,
                                  &epriv);
  priv = epriv.ptr;

  if (ret < 0 || !_gnutls_version_has_selectable_sighash (ver)
      || priv->sign_algorithms_size == 0)
    /* none set, allow SHA-1 only */
    {
      return _gnutls_x509_pk_to_sign (cert->subject_pk_algorithm, GNUTLS_DIG_SHA1);
    }

  for (i = 0; i < priv->sign_algorithms_size; i++)
    {
      if (_gnutls_sign_get_pk_algorithm (priv->sign_algorithms[i]) == cert->subject_pk_algorithm)
        {
          if (cert_compatible_with_sig(cert, ver, priv->sign_algorithms[i]) < 0)
            continue;

          return priv->sign_algorithms[i];
        }
    }

  return GNUTLS_SIGN_UNKNOWN;
}


/* Check if the given signature algorithm is accepted by
 * the peer. Returns 0 on success or a negative value
 * on error.
 */
int
_gnutls_session_sign_algo_requested (gnutls_session_t session,
                                     gnutls_sign_algorithm_t sig)
{
  unsigned i;
  int ret, hash;
  gnutls_protocol_t ver = gnutls_protocol_get_version (session);
  sig_ext_st *priv;
  extension_priv_data_t epriv;

  if (!_gnutls_version_has_selectable_sighash (ver))
    {
      return 0;
    }

  ret =
    _gnutls_ext_get_session_data (session,
                                  GNUTLS_EXTENSION_SIGNATURE_ALGORITHMS,
                                  &epriv);
  if (ret < 0)
    {
      gnutls_assert ();
      /* extension not received allow SHA1 and SHA256 */
      hash = _gnutls_sign_get_hash_algorithm (sig);
      if (hash == GNUTLS_DIG_SHA1 || hash == GNUTLS_DIG_SHA256)
        return 0;
      else
        return ret;
    }
  priv = epriv.ptr;

  if (priv->sign_algorithms_size == 0)
    /* none set, allow all */
    {
      return 0;
    }

  for (i = 0; i < priv->sign_algorithms_size; i++)
    {
      if (priv->sign_algorithms[i] == sig)
        {
          return 0;             /* ok */
        }
    }

  return GNUTLS_E_UNSUPPORTED_SIGNATURE_ALGORITHM;
}

/* Check if the given signature algorithm is supported.
 * This means that it is enabled by the priority functions,
 * and in case of a server a matching certificate exists.
 */
int
_gnutls_session_sign_algo_enabled (gnutls_session_t session,
                                   gnutls_sign_algorithm_t sig)
{
  unsigned i;
  int ret;
  gnutls_protocol_t ver = gnutls_protocol_get_version (session);
  sig_ext_st *priv;
  extension_priv_data_t epriv;

  ret =
    _gnutls_ext_get_session_data (session,
                                  GNUTLS_EXTENSION_SIGNATURE_ALGORITHMS,
                                  &epriv);
  if (ret < 0)
    {
      gnutls_assert ();
      return 0;
    }
  priv = epriv.ptr;

  if (!_gnutls_version_has_selectable_sighash (ver)
      || priv->sign_algorithms_size == 0)
    /* none set, allow all */
    {
      return 0;
    }

  for (i = 0; i < session->internals.priorities.sign_algo.algorithms; i++)
    {
      if (session->internals.priorities.sign_algo.priority[i] == sig)
        {
          return 0;             /* ok */
        }
    }

  return GNUTLS_E_UNSUPPORTED_SIGNATURE_ALGORITHM;
}

static void
signature_algorithms_deinit_data (extension_priv_data_t priv)
{
  gnutls_free (priv.ptr);
}

static int
signature_algorithms_pack (extension_priv_data_t epriv, gnutls_buffer_st * ps)
{
  sig_ext_st *priv = epriv.ptr;
  int ret, i;

  BUFFER_APPEND_NUM (ps, priv->sign_algorithms_size);
  for (i = 0; i < priv->sign_algorithms_size; i++)
    {
      BUFFER_APPEND_NUM (ps, priv->sign_algorithms[i]);
    }
  return 0;
}

static int
signature_algorithms_unpack (gnutls_buffer_st * ps,
                             extension_priv_data_t * _priv)
{
  sig_ext_st *priv;
  int i, ret;
  extension_priv_data_t epriv;

  priv = gnutls_calloc (1, sizeof (*priv));
  if (priv == NULL)
    {
      gnutls_assert ();
      return GNUTLS_E_MEMORY_ERROR;
    }

  BUFFER_POP_NUM (ps, priv->sign_algorithms_size);
  for (i = 0; i < priv->sign_algorithms_size; i++)
    {
      BUFFER_POP_NUM (ps, priv->sign_algorithms[i]);
    }

  epriv.ptr = priv;
  *_priv = epriv;

  return 0;

error:
  gnutls_free (priv);
  return ret;
}



/**
 * gnutls_sign_algorithm_get_requested:
 * @session: is a #gnutls_session_t structure.
 * @indx: is an index of the signature algorithm to return
 * @algo: the returned certificate type will be stored there
 *
 * Returns the signature algorithm specified by index that was
 * requested by the peer. If the specified index has no data available
 * this function returns %GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE.  If
 * the negotiated TLS version does not support signature algorithms
 * then %GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE will be returned even
 * for the first index.  The first index is 0.
 *
 * This function is useful in the certificate callback functions
 * to assist in selecting the correct certificate.
 *
 * Returns: On success, %GNUTLS_E_SUCCESS (0) is returned, otherwise
 *   an error code is returned.
 *
 * Since: 2.10.0
 **/
int
gnutls_sign_algorithm_get_requested (gnutls_session_t session,
                                     size_t indx,
                                     gnutls_sign_algorithm_t * algo)
{
  gnutls_protocol_t ver = gnutls_protocol_get_version (session);
  sig_ext_st *priv;
  extension_priv_data_t epriv;
  int ret;

  ret =
    _gnutls_ext_get_session_data (session,
                                  GNUTLS_EXTENSION_SIGNATURE_ALGORITHMS,
                                  &epriv);
  if (ret < 0)
    {
      gnutls_assert ();
      return ret;
    }
  priv = epriv.ptr;

  if (!_gnutls_version_has_selectable_sighash (ver)
      || priv->sign_algorithms_size == 0)
    {
      return GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE;
    }

  if (indx < priv->sign_algorithms_size)
    {
      *algo = priv->sign_algorithms[indx];
      return 0;
    }
  else
    return GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE;
}
