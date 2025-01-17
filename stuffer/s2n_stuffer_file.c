/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>

#include "error/s2n_errno.h"
#include "stuffer/s2n_stuffer.h"
#include "utils/s2n_safety.h"

int s2n_stuffer_recv_from_fd(struct s2n_stuffer *stuffer, const int rfd, const uint32_t len, uint32_t *bytes_written)
{
    POSIX_PRECONDITION(s2n_stuffer_validate(stuffer));

    uint32_t rlen = len;
    /* Make sure we have enough space to write */
    POSIX_GUARD(s2n_stuffer_skip_write(stuffer, len));
    if (SSIZE_MAX < UINT32_MAX && len > SSIZE_MAX)
        rlen = SSIZE_MAX;
    else if (len > UINT32_MAX)
        rlen = UINT32_MAX;

    /* "undo" the skip write */
    stuffer->write_cursor -= rlen;

    ssize_t r = 0;
    do {
        POSIX_ENSURE(stuffer->blob.data && (r >= 0 || errno == EINTR), S2N_ERR_READ);
        r = read(rfd, stuffer->blob.data + stuffer->write_cursor, rlen);
    } while (r < 0);

    /* Record just how many bytes we have written */
    POSIX_GUARD(s2n_stuffer_skip_write(stuffer, (uint32_t) r));
    if (bytes_written != NULL) {
        *bytes_written = r;
    }
    return S2N_SUCCESS;
}

int s2n_stuffer_send_to_fd(struct s2n_stuffer *stuffer, const int wfd, const uint32_t len, uint32_t *bytes_sent)
{
    POSIX_PRECONDITION(s2n_stuffer_validate(stuffer));

    uint32_t rlen = len;
    /* Make sure we even have the data */
    POSIX_GUARD(s2n_stuffer_skip_read(stuffer, len));
    if (SSIZE_MAX < UINT32_MAX && len > SSIZE_MAX)
        rlen = SSIZE_MAX;
    else if (len > UINT32_MAX)
        rlen = UINT32_MAX;
    if (stuffer->read_cursor > UINT32_MAX - rlen)
        rlen = UINT32_MAX - stuffer->read_cursor;

    /* "undo" the skip read */
    stuffer->read_cursor -= rlen;

    ssize_t w = 0;
    do {
        POSIX_ENSURE(stuffer->blob.data && (w >= 0 || errno == EINTR), S2N_ERR_WRITE);
        w = write(wfd, stuffer->blob.data + stuffer->read_cursor, rlen);
    } while (w < 0);

    stuffer->read_cursor += w;
    if (bytes_sent != NULL) {
        *bytes_sent = w;
    }
    return S2N_SUCCESS;
}

int s2n_stuffer_alloc_ro_from_fd(struct s2n_stuffer *stuffer, int rfd)
{
    POSIX_ENSURE_MUT(stuffer);
    struct stat st = { 0 };

    POSIX_ENSURE(fstat(rfd, &st) >= 0, S2N_ERR_FSTAT);

    POSIX_ENSURE_GT(st.st_size, 0);
    POSIX_ENSURE_LTE(st.st_size, UINT32_MAX);

    uint8_t *map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, rfd, 0);
    POSIX_ENSURE(map != MAP_FAILED, S2N_ERR_MMAP);

    struct s2n_blob b = { 0 };
    POSIX_GUARD(s2n_blob_init(&b, map, (uint32_t) st.st_size));
    return s2n_stuffer_init(stuffer, &b);
}

int s2n_stuffer_alloc_ro_from_file(struct s2n_stuffer *stuffer, const char *file)
{
    POSIX_ENSURE_MUT(stuffer);
    POSIX_ENSURE_REF(file);
    int fd;

    do {
        fd = open(file, O_RDONLY);
        POSIX_ENSURE(fd >= 0 || errno == EINTR, S2N_ERR_OPEN);
    } while (fd < 0);

    int r = s2n_stuffer_alloc_ro_from_fd(stuffer, fd);

    POSIX_GUARD(close(fd));

    return r;
}
