#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>
#define HASH_BYTES 32 // Assume we are using SHA-256 for hashing file chunks - produces a 256 bit value, or 32 bytes

/**
 * Struct to hold a chunk of a file and its corresponding hash.
 */
typedef struct chunks_with_hashes
{
    unsigned char *hash;
    char *chunk;
    int chunk_length;
} chunks_with_hashes;
/**
 * Send over socket, ensuring that all data is sent.
 * Returns true if all data was sent successfully, false otherwise.
 */
bool send_over_socket(int socket, unsigned char *data, int data_length)
{
    int bytes_sent = 0;
    int bytes_left = data_length;
    while (bytes_left > 0)
    {
        int res = send(socket, data + bytes_sent, bytes_left, 0);
        if (res <= 0)
        {
            return false;
        }
        bytes_sent += res;
        bytes_left -= res;
    }
    return true;
}

/**
 * Receive data over socket, ensuring that all data is received.
 * Returns true if all data was received successfully, false otherwise.
 */
bool recv_over_socket(int socket, unsigned char *buffer, int buffer_length)
{
    int bytes_received = 0;
    int bytes_left = buffer_length;
    while (bytes_left > 0)
    {
        int res = recv(socket, buffer + bytes_received, bytes_left, 0);
        if (res <= 0)
        {
            return false;
        }
        bytes_received += res;
        bytes_left -= res;
    }
    return true;
}

/**
 * Given a socket, file path, and array of chunks of the file with hashes,
 * compare the hashes with those on the server side, and send across
 * any missing chunks to the server.
 */
void updateFile(int socket, unsigned char *file_path, int file_path_length, struct chunks_with_hashes *chunks, int num_chunks)
{
    // Notify server which file we are handling
    bool res_send_file_path = send_over_socket(socket, file_path, file_path_length);
    if (res_send_file_path == false)
    {
        perror("failed to send file path data over socket");
        exit(EXIT_FAILURE);
    }

    // Query server for missing chunk hashes
    int hash_size = num_chunks * HASH_BYTES;
    int total_buf_size = sizeof(uint8_t) * hash_size + sizeof(uint32_t);

    uint8_t *hash_buf;
    hash_buf = (uint8_t *)malloc(total_buf_size);
    assert(hash_buf);

    // Write the number of chunks to the buffer so the server knows how many hashes to expect,
    // then write the hashes of the chunks to the buffer
    uint32_t chunks_to_write = htonl(num_chunks);
    memcpy(hash_buf, &chunks_to_write, sizeof(uint32_t));

    uint8_t *bufferPtr = hash_buf + sizeof(uint32_t);
    for (int i = 0; i < num_chunks; ++i)
    {
        memcpy(bufferPtr, chunks[i].hash, HASH_BYTES);
        bufferPtr += HASH_BYTES;
    }
    bool res_send_hashes = send_over_socket(socket, hash_buf, total_buf_size);
    free(hash_buf);
    if (res_send_hashes == false)
    {
        perror("failed to send chunk hashes over socket");
        exit(EXIT_FAILURE);
    }

    // Read number of missing chunks from server
    int num_missing_chunks_raw = 0;

    int res_num_missing_chunks = recv(socket, &num_missing_chunks_raw, sizeof(int), 0);
    if (res_num_missing_chunks <= 0)
    {
        perror("failed to receive number of missing chunks from server");
        exit(EXIT_FAILURE);
    }
    int num_missing_chunks = ntohl(num_missing_chunks_raw);

    if (num_missing_chunks == 0)
    {
        // No missing chunks for file, nothing to send to server
        return;
    }
    else if (num_missing_chunks < 0)
    {
        perror("server reported an error while processing file");
        exit(EXIT_FAILURE);
    }

    // Read the missing hashes from the server
    unsigned char *missing_hashes = (unsigned char *)malloc(HASH_BYTES * num_missing_chunks);
    assert(missing_hashes);
    bool res_missing_hashes = recv_over_socket(socket, missing_hashes, num_missing_chunks * HASH_BYTES);
    if (res_missing_hashes == false)
    {
        free(missing_hashes);
        perror("failed to receive missing chunk hashes from server");
        exit(EXIT_FAILURE);
    }

    // Transfer across the missing chunks to the server
    for (int i = 0; i < num_missing_chunks; ++i)
    {
        for (int j = 0; j < num_chunks; ++j)
        {
            if (memcmp(missing_hashes + i * HASH_BYTES, chunks[j].hash, HASH_BYTES) == 0)
            {
                uint8_t *buffer;
                int buffer_size = HASH_BYTES + chunks[j].chunk_length + sizeof(uint32_t);
                buffer = (uint8_t *)malloc(buffer_size);
                assert(buffer);

                uint8_t *buffer_ptr = buffer;
                memcpy(buffer_ptr, chunks[j].hash, HASH_BYTES);
                buffer_ptr += HASH_BYTES;

                uint32_t chunk_length = htonl(chunks[j].chunk_length);
                memcpy(buffer_ptr, &chunk_length, sizeof(uint32_t));
                buffer_ptr += sizeof(uint32_t);

                memcpy(buffer_ptr, chunks[j].chunk, chunks[j].chunk_length);
                bool res = send_over_socket(socket, buffer, buffer_size);
                if (res == false)
                {
                    free(buffer);
                    free(missing_hashes);
                    perror("failed to send file chunk over socket");
                    exit(EXIT_FAILURE);
                }
                free(buffer);
            }
        }
    }

    free(missing_hashes);
}
