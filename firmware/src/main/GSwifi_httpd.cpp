/* Copyright (C) 2013 gsfan, MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/** @file
 * @brief Gainspan wi-fi module library for mbed
 * GS1011MIC, GS1011MIP, GainSpan WiFi Breakout, etc.
 */

#include "GSwifi_conf.h"

#include "Arduino.h"
#include "GSwifi.h"
#include "pgmStrToRAM.h"
#include "MemoryFree.h"

int GSwifi::httpd (int port) {
    int i;
    char cmd[GS_CMD_SIZE];

    if (! _connect || _power_status != GSPOWERSTATUS_READY) {
        return -1;
    }

    memset(&_httpd, 0, sizeof(_httpd));
    for (i = 0; i < GS_HTTPD_PORT_COUNT; i ++) {
        _httpd[i].mode = GSHTTPDMODE_REQUEST;
    }
    _handler_count = 0;

    sprintf(cmd, P("AT+NSTCP=%d"), port);

    // parseCmdResponse fills _cid
    command(cmd, GSRES_CONNECT);
    if (did_timeout_) {
        return -1;
    }

    newSock(_cid, GSTYPE_SERVER, GSPROT_HTTPD, this, &GSwifi::poll_httpd);
    return _cid;
}

void GSwifi::poll_httpd (int cid, int len) {
    int i, j, flg = 0;
    char c;

    if (len == 0) {
        // start request
        _httpd[cid].mode      = GSHTTPDMODE_REQUEST;
        _httpd[cid].len       = 0;
        _httpd[cid].keepalive = 0;
        return;
    }

    while (_gs_sock[cid].connect && (! _gs_sock[cid].data->isEmpty())) {
        flg = 0;
        if (_httpd[cid].buf == NULL) {
            _httpd[cid].buf = (char*)malloc(HTTPD_BUF_SIZE);
        }
        // get 1 line
        for (j = 0; j < len; j ++) {
            _gs_sock[cid].data->dequeue(&c);
            if (c == '\r') continue;
            if (c == '\n' && _httpd[cid].mode != GSHTTPDMODE_BODY) break;

            if (_httpd[cid].len < HTTPD_BUF_SIZE - 1) {
                _httpd[cid].buf[_httpd[cid].len] = c;
            }
            _httpd[cid].len ++;
            if (_httpd[cid].mode == GSHTTPDMODE_BODY &&
                _httpd[cid].len >= _httpd[cid].length) {
                break; // end of body
            }
        }
        if (j >= len) {
            return;
        }
        if (_httpd[cid].len < HTTPD_BUF_SIZE) {
            _httpd[cid].buf[_httpd[cid].len] = 0;
            // DBG("httpd %d: %d %s (%d)\r\n", cid, _httpd[cid].mode, _httpd[cid].buf, _httpd[cid].len);
        }

        // parse
        switch (_httpd[cid].mode) {
        case GSHTTPDMODE_REQUEST:
            if (strnicmp(_httpd[cid].buf, "GET ", 4) == 0) {
                _httpd[cid].type = GSPROT_HTTPGET;
                j = 4;
            } else
                if (strnicmp(_httpd[cid].buf, "POST ", 5) == 0) {
                    _httpd[cid].type = GSPROT_HTTPPOST;
                    j = 5;
                } else {
                    _httpd[cid].mode = GSHTTPDMODE_ERROR;
                    break;
                }

            // get uri
            for (i = j; i < _httpd[cid].len; i ++) {
                if (_httpd[cid].buf[i] == ' ') break;
            }
            i = i - j;
            if (i) {
                if (_httpd[cid].uri == NULL) {
                    _httpd[cid].uri = (char*)malloc(HTTPD_URI_SIZE);
                }
                strncpy(_httpd[cid].uri, &_httpd[cid].buf[j], i);
                _httpd[cid].uri[i] = 0;
            }
            _httpd[cid].mode = GSHTTPDMODE_HEAD;
            _httpd[cid].length = 0;
            // DBG("uri: %s\r\n", _httpd[cid].uri);
            break;

        case GSHTTPDMODE_HEAD:
            if (_httpd[cid].len == 0) {
                // blank line (end of header)
                _httpd[cid].mode = GSHTTPDMODE_BODY;
                if (_httpd[cid].length == 0) {
                    flg = 1; // no body
                }
            } else if (strnicmp(_httpd[cid].buf, "Content-Length: ", 16) == 0) {
                _httpd[cid].length = atoi(&_httpd[cid].buf[16]);
            } else if (strnicmp(_httpd[cid].buf, "Connection: Keep-Alive", 22) == 0) {
                if (! _httpd[cid].keepalive) {
                    _httpd[cid].keepalive = HTTPD_KEEPALIVE;
                }
            }
            break;

        case GSHTTPDMODE_BODY:
            if (_httpd[cid].len >= _httpd[cid].length) {
                // DBG("body: %s\r\n", _httpd[cid].buf);
                flg = 1;
            }
            break;

        }

        if (flg) {
            // http request
            _httpd[cid].buf[_httpd[cid].len] = 0;

            i = get_handler(_httpd[cid].uri);
            if (i >= 0) {
                _httpd[cid].host = _gs_sock[cid].host;
                j = strlen(_handler[i].uri);
                _httpd[cid].file = &_httpd[cid].uri[j];
                _httpd[cid].query = NULL;
                for (; j < strlen(_httpd[cid].uri); j ++) {
                    if (_httpd[cid].uri[j] == '?') {
                        // query string
                        _httpd[cid].uri[j] = 0;
                        _httpd[cid].query = &_httpd[cid].uri[j + 1];
                        break;
                    }
                }

                if (_handler[i].onHttpCgi) {
                    // cgi
                    _handler[i].onHttpCgi(cid, &_httpd[cid]);
                    _httpd[cid].keepalive = 0;
                    // LOG("%d.%d.%d.%d ", _httpd[cid].host.getIp()[0], _httpd[cid].host.getIp()[1], _httpd[cid].host.getIp()[2], _httpd[cid].host.getIp()[3]);
                    // LOG("%s %s %d 200 -\r\n", _httpd[cid].type == GSPROT_HTTPGET ? "GET" : "POST", _httpd[cid].uri, _httpd[cid].length);
                    flg = 1;
                }
            } else {
                // not found
                send_httpd_error(cid, 403);
            }

            if (_httpd[cid].keepalive) {
                _httpd[cid].mode = GSHTTPDMODE_REQUEST;
                _httpd[cid].len = 0;
                _httpd[cid].length = 0;
                _httpd[cid].keepalive --;
            } else {
                close(cid);
            }
        }

        if (_httpd[cid].mode == GSHTTPDMODE_ERROR) {
            send_httpd_error(cid, 400);
        }

        _httpd[cid].len = 0;
    } // while
}

int GSwifi::get_handler (char *uri) {
    int i, j;

    for (i = 0; i < _handler_count; i ++) {
        j = strlen(_handler[i].uri);
        if (strncmp(uri, _handler[i].uri, j) == NULL) {
            // found
            return i;
        }
    }
    return -1;
}

int GSwifi::strnicmp (const char *p1, const char *p2, int n) {
    int i, r = -1;
    char c1, c2;

    for (i = 0; i < n; i ++) {
        c1 = (p1[i] >= 'a' && p1[i] <= 'z') ? p1[i] - ('a' - 'A'): p1[i];
        c2 = (p2[i] >= 'a' && p2[i] <= 'z') ? p2[i] - ('a' - 'A'): p2[i];
        r = c1 - c2;
        if (r) break;
    }
    return r;
}

void GSwifi::send_httpd_error (int cid, int err) {
    char buf[100], msg[30];

    switch (err) {
    case 400:
        strcpy(msg, "Bad Request");
        break;
    case 403:
        strcpy(msg, "Forbidden");
        break;
    case 404:
        strcpy(msg, "Not Found");
        break;
    case 500:
    default:
        strcpy(msg, "Internal Server Error");
        break;
    }
    // DBG("httpd error: %d %d %s\r\n", cid, err, msg);

    // sprintf(buf, "HTTP/1.1 %d %s\r\n", err, msg);
    // send(cid, buf, strlen(buf));
    // strcpy(buf, "Content-Type: text/html\r\n");
    // send(cid, buf, strlen(buf));
    // send(cid, "\r\n", 2);

    // sprintf(buf, "<html><head><title>%d %s</title></head>\r\n", err, msg);
    // send(cid, buf, strlen(buf));
    // sprintf(buf, "<body><h1>%s</h1></body></html>\r\n", msg);
    // send(cid, buf, strlen(buf));
    // close(cid);
    // LOG("%d.%d.%d.%d ", _httpd[cid].host.getIp()[0], _httpd[cid].host.getIp()[1], _httpd[cid].host.getIp()[2], _httpd[cid].host.getIp()[3]);
    // LOG("%s %s %d %d -\r\n", _httpd[cid].type == GSPROT_HTTPGET ? "GET" : "POST", _httpd[cid].uri, _httpd[cid].length, err);
}

int GSwifi::attach_httpd (const char *uri, onHttpdCgiFunc ponHttpCgi) {
    if (_handler_count < GS_HTTPD_REQUEST_HANDLER_COUNT) {
        _handler[_handler_count].uri = (char*)malloc(strlen(uri) + 1);
        strcpy(_handler[_handler_count].uri, uri);
        _handler[_handler_count].onHttpCgi = ponHttpCgi;
        _handler_count ++;
        return 0;
    } else {
        return -1;
    }
}