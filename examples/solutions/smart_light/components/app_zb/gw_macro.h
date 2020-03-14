#ifndef GW_MACRO__H
#define GW_MACRO__H


#define LOGGING_QUEUE_LENGTH (8)
#define ZB_CHANNEL_DEFAULT			(13)
#define ZB_EXTPANID_DEFAULT			(0x1234567890abcdefU)

/*! Returns a uint16_t from a buffer, little-endian */
#define Utils_ExtractTwoByteValue(buf) \
    ((*(buf)) | ( (*((buf) + 1)) << 8) )

/*! Returns a 3-byte value from a buffer, little-endian */
#define Utils_ExtractThreeByteValue(buf) \
( \
    (*(buf)) \
    | ( (*((buf) + 1)) << 8) \
    | ( (*((buf) + 2)) << 16) \
)

/*! Returns a uint32_t from a buffer, little-endian */
#define Utils_ExtractFourByteValue(buf) \
( \
    (*(buf)) \
    | ( (*((buf) + 1)) << 8) \
    | ( (*((buf) + 2)) << 16) \
    | ( (*((buf) + 3)) << 24) \
)

/*! Returns a uint16_t from a buffer, big-endian */
#define Utils_BeExtractTwoByteValue(buf) \
    ((*((buf) + 1)) | ( (*(buf)) << 8) )

/*! Returns a 3-byte value from a buffer, big-endian */
#define Utils_BeExtractThreeByteValue(buf) \
( \
    (*((buf) + 2)) \
    | ( (*((buf) + 1)) << 8) \
    | ( (*(buf)) << 16) \
)

/*! Returns a uint32_t from a buffer, big-endian */
#define Utils_BeExtractFourByteValue(buf) \
( \
    (*((buf) + 3)) \
    | ( (*((buf) + 2)) << 8) \
    | ( (*((buf) + 1)) << 16) \
    | ( (*(buf)) << 24) \
)

/*! Writes a uint16_t into a buffer, little-endian */
#define Utils_PackTwoByteValue(value, buf) \
{ \
    (buf)[0] = (uint8_t) ((value) & 0xFF); \
    (buf)[1] = (uint8_t) (((value) >> 8) & 0xFF); \
}

/*! Writes a 3-byte value into a buffer, little-endian */
#define Utils_PackThreeByteValue(value, buf) \
{ \
    (buf)[0] = (uint8_t) ((value) & 0xFF); \
    (buf)[1] = (uint8_t) (((value) >> 8) & 0xFF); \
    (buf)[2] = (uint8_t) (((value) >> 16) & 0xFF); \
}

/*! Writes a uint32_t into a buffer, little-endian */
#define Utils_PackFourByteValue(value, buf) \
{ \
    (buf)[0] = (uint8_t) ((value) & 0xFF); \
    (buf)[1] = (uint8_t) (((value) >> 8) & 0xFF); \
    (buf)[2] = (uint8_t) (((value) >> 16) & 0xFF); \
    (buf)[3] = (uint8_t) (((value) >> 24) & 0xFF); \
}

/*! Writes a uint16_t into a buffer, big-endian */
#define Utils_BePackTwoByteValue(value, buf) \
{ \
    (buf)[1] = (uint8_t) ((value) & 0xFF); \
    (buf)[0] = (uint8_t) (((value) >> 8) & 0xFF); \
}

/*! Writes a 3-byte value into a buffer, big-endian */
#define Utils_BePackThreeByteValue(value, buf) \
{ \
    (buf)[2] = (uint8_t) ((value) & 0xFF); \
    (buf)[1] = (uint8_t) (((value) >> 8) & 0xFF); \
    (buf)[0] = (uint8_t) (((value) >> 16) & 0xFF); \
}

/*! Writes a uint32_t into a buffer, big-endian */
#define Utils_BePackFourByteValue(value, buf) \
{ \
    (buf)[3] = (uint8_t) ((value) & 0xFF); \
    (buf)[2] = (uint8_t) (((value) >> 8) & 0xFF); \
    (buf)[1] = (uint8_t) (((value) >> 16) & 0xFF); \
    (buf)[0] = (uint8_t) (((value) >> 24) & 0xFF); \
}



/*! Reverts the order of bytes in an array - useful for changing the endianness */
#define Utils_RevertByteArray(array, size) \
{ \
    uint8_t temp; \
    for (uint8_t j = 0; j < (size)/2; j++) \
    { \
        temp = (array)[j]; \
        (array)[j] = (array)[(size) - 1 - j]; \
        (array)[(size) - 1 - j] = temp; \
    } \
}


#endif
