// Copyright (c) 2015-2022 The BTQ Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BTQ_REST_H
#define BTQ_REST_H

#include <string>

enum class RESTResponseFormat {
    UNDEF,
    BINARY,
    HEX,
    JSON,
};

/**
 * Parse a URI to get the data format and URI without data format
 * and query string.
 *
 * @param[out]  param   The strReq without the data format string and
 *                      without the query string (if any).
 * @param[in]   strReq  The URI to be parsed.
 * @return      RESTResponseFormat that was parsed from the URI.
 */
RESTResponseFormat ParseDataFormat(std::string& param, const std::string& strReq);

#endif // BTQ_REST_H
