/*
 * GeniePiLib.cpp
 *
 *  GeniePiLib.h implementasyonu. (GeniePi.h header-only surumuyle
 *  BIREBIR AYNI davranisa sahiptir, sadece ayri derleniyor.)
 */

#include "GeniePiLib.h"



int GeniePi::genieOpen(char *device, int baud)
{
    struct termios options;
    speed_t myBaud;
    int status, fd;

    switch (baud)
    {
        case     50: myBaud =     B50; break;
        case     75: myBaud =     B75; break;
        case    110: myBaud =    B110; break;
        case    134: myBaud =    B134; break;
        case    150: myBaud =    B150; break;
        case    200: myBaud =    B200; break;
        case    300: myBaud =    B300; break;
        case    600: myBaud =    B600; break;
        case   1200: myBaud =   B1200; break;
        case   1800: myBaud =   B1800; break;
        case   2400: myBaud =   B2400; break;
        case   9600: myBaud =   B9600; break;
        case  19200: myBaud =  B19200; break;
        case  38400: myBaud =  B38400; break;
        case  57600: myBaud =  B57600; break;
        case 115200: myBaud = B115200; break;
        case 230400: myBaud = B230400; break;
        default:
            return -2;
    }

    if ((fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY | O_NONBLOCK)) == -1)
        return -1;

    fcntl(fd, F_SETFL, O_RDWR);

    tcgetattr(fd, &options);

    cfmakeraw(&options);
    cfsetispeed(&options, myBaud);
    cfsetospeed(&options, myBaud);

    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_oflag &= ~OPOST;

    options.c_cc[VMIN]  = 0;
    options.c_cc[VTIME] = 100;

    tcsetattr(fd, TCSANOW | TCSAFLUSH, &options);

    ioctl(fd, TIOCMGET, &status);
    status |= TIOCM_DTR;
    status |= TIOCM_RTS;
    ioctl(fd, TIOCMSET, &status);

    usleep(10000);

    return fd;
}

void GeniePi::genieFlush(int fd)
{
    tcflush(fd, TCIOFLUSH);
}

void GeniePi::genieClose(void)
{
    listenerRunning.store(false);
    if (listenerThread.joinable())
        listenerThread.join();

    if (genieFd != -1)
    {
        close(genieFd);
        genieFd = -1;
    }
}

int GeniePi::genieDataAvail(int fd)
{
    int result;
    if (ioctl(fd, FIONREAD, &result) == -1)
        return -1;
    return result;
}

unsigned int GeniePi::millis(void)
{
    struct timeval tv;
    unsigned long long t1;

    gettimeofday(&tv, NULL);
    t1 = (tv.tv_sec * 1000000ULL + tv.tv_usec) / 1000;

    return (unsigned int)(t1 - epoch);
}

void GeniePi::delay(unsigned int howLong)
{
    struct timespec sleeper, dummy;
    sleeper.tv_sec  = (time_t)(howLong / 1000);
    sleeper.tv_nsec = (long)(howLong % 1000) * 1000000;
    nanosleep(&sleeper, &dummy);
}

void GeniePi::delayMicroseconds(unsigned int howLong)
{
    struct timespec sleeper, dummy;
    sleeper.tv_sec  = 0;
    sleeper.tv_nsec = (long)(howLong * 1000);
    nanosleep(&sleeper, &dummy);
}

int GeniePi::genieGetchar(void)
{
    unsigned int timeUp = millis() + 5;
    unsigned char x;

    while (millis() < timeUp)
    {
        if (genieDataAvail(genieFd))
        {
            if (read(genieFd, &x, 1) == 1)
                return ((int)x) & 0xFF;
            return -1;
        }
        else
            delayMicroseconds(101);
    }
    return -1;
}

void GeniePi::geniePutchar(int data)
{
    unsigned char c = (unsigned char)data;
    write(genieFd, &c, 1);
}

void GeniePi::genieReplyListener(void)
{
    unsigned int totalLength = 0, readLength;
    int byteData[100];

    while (genieFd == -1)
        delay(1);

    while (listenerRunning.load())
    {
        int cmd;
        while ((cmd = genieGetchar()) == -1)
        {
            if (!listenerRunning.load())
                return;
        }

        if (cmd == GENIE_ACK) { genieAck.store(true); continue; }
        if (cmd == GENIE_NAK) { genieNak.store(true); continue; }

        unsigned char csum = (unsigned char)cmd;

        int object = genieGetchar();
        if (object == -1) { ++genieTimeouts; continue; }
        csum ^= (unsigned char)object;

        int index = genieGetchar();
        if (index == -1) { ++genieTimeouts; continue; }
        csum ^= (unsigned char)index;

        int msb = 0, lsb = 0;
        bool timedOut = false;

        if (cmd == GENIE_REPORT_MAGIC_BYTES || cmd == GENIE_REPORT_DOUBLE_BYTES)
        {
            totalLength = (unsigned int)index;
            if (cmd == GENIE_REPORT_DOUBLE_BYTES)
                totalLength = (unsigned int)index * 2;

            for (readLength = 0; readLength < totalLength; readLength++)
            {
                byteData[readLength] = genieGetchar();
                if (byteData[readLength] == -1) { ++genieTimeouts; timedOut = true; break; }
                csum ^= (unsigned char)byteData[readLength];
            }
            if (timedOut)
                continue;
        }
        else
        {
            msb = genieGetchar();
            if (msb == -1) { ++genieTimeouts; continue; }
            csum ^= (unsigned char)msb;

            lsb = genieGetchar();
            if (lsb == -1) { ++genieTimeouts; continue; }
            csum ^= (unsigned char)lsb;
        }

        int recvChecksum = genieGetchar();
        if (recvChecksum == -1 || (unsigned char)recvChecksum != csum)
        {
            ++genieChecksumErrors;
            continue;
        }

        int next = (genieReplysHead + 1) & (MAX_GENIE_REPLYS - 1);

        if (cmd == GENIE_REPORT_MAGIC_BYTES || cmd == GENIE_REPORT_DOUBLE_BYTES)
        {
            if (next != genieReplysTail)
            {
                genieMagicReplyStruct *magicByteReply = &genieMagicReplys[genieReplysHead];
                magicByteReply->cmd    = cmd;
                magicByteReply->index  = object;
                magicByteReply->length = index;

                if (cmd == GENIE_REPORT_MAGIC_BYTES)
                {
                    for (readLength = 0; readLength < totalLength; readLength++)
                        magicByteReply->data[readLength] = (unsigned int)byteData[readLength];
                }

                if (cmd == GENIE_REPORT_DOUBLE_BYTES)
                {
                    for (readLength = 1; readLength < totalLength; readLength++)
                        magicByteReply->data[readLength] =
                            (unsigned int)((byteData[readLength * 2] << 8) |
                                            byteData[(readLength * 2) + 1]);
                }

                genieReplysHead = next;
            }
        }
        else
        {
            if (next != genieReplysTail)
            {
                genieReplyStruct *reply = &genieReplys[genieReplysHead];
                reply->cmd    = cmd;
                reply->object = object;
                reply->index  = index;
                reply->data   = (unsigned int)((msb << 8) | lsb);
                genieReplysHead = next;
            }
        }
    }
}

int GeniePi::genieReplyAvail(void)
{
    return (genieReplysHead != genieReplysTail);
}

void GeniePi::genieGetReply(struct genieReplyStruct *reply)
{
    while (!genieReplyAvail())
        delay(1);

    std::memcpy(reply, &genieReplys[genieReplysTail], sizeof(struct genieReplyStruct));
    genieReplysTail = (genieReplysTail + 1) & (MAX_GENIE_REPLYS - 1);
}

int GeniePi::_genieReadObj(int object, int index)
{
    struct genieReplyStruct reply;
    unsigned int timeUp;
    unsigned char checksum;

    while (genieReplyAvail())
        genieGetReply(&reply);

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_READ_OBJ); checksum  = GENIE_READ_OBJ;
    geniePutchar(object);         checksum ^= (unsigned char)object;
    geniePutchar(index);          checksum ^= (unsigned char)index;
    geniePutchar(checksum);

    for (timeUp = millis() + 50; millis() < timeUp; )
    {
        if (genieNak.load())
            return -1;

        if (genieReplyAvail())
        {
            genieGetReply(&reply);
            if ((reply.cmd == GENIE_REPORT_OBJ) && (reply.object == object) && (reply.index == index))
                return reply.data;
        }
        delayMicroseconds(101);
    }
    return -1;
}

int GeniePi::genieReadObj(int object, int index)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieReadObj(object, index);
}

int GeniePi::_genieWriteObj(int object, int index, unsigned int data)
{
    unsigned char checksum, msb, lsb;

    lsb = (unsigned char)((data >> 0) & 0xFF);
    msb = (unsigned char)((data >> 8) & 0xFF);

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_WRITE_OBJ); checksum  = GENIE_WRITE_OBJ;
    geniePutchar(object);          checksum ^= (unsigned char)object;
    geniePutchar(index);           checksum ^= (unsigned char)index;
    geniePutchar(msb);             checksum ^= msb;
    geniePutchar(lsb);             checksum ^= lsb;
    geniePutchar(checksum);

    while (!genieAck.load() && !genieNak.load())
        delay(1);

    return 0;
}

int GeniePi::genieWriteObj(int object, int index, unsigned int data)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteObj(object, index, data);
}

int GeniePi::genieWriteShortToIntLedDigits(int index, int16_t data)
{
    return genieWriteObj(GENIE_OBJ_ILED_DIGITS_L, index, data);
}

int GeniePi::genieWriteFloatToIntLedDigits(int index, float data)
{
    union FloatLongFrame frame;
    frame.floatValue = data;
    int retval = genieWriteObj(GENIE_OBJ_ILED_DIGITS_H, index, frame.wordValue[1]);
    if (retval != 1) return retval;
    return genieWriteObj(GENIE_OBJ_ILED_DIGITS_L, index, frame.wordValue[0]);
}

int GeniePi::genieWriteLongToIntLedDigits(int index, int32_t data)
{
    union FloatLongFrame frame;
    frame.longValue = data;
    int retval = genieWriteObj(GENIE_OBJ_ILED_DIGITS_H, index, frame.wordValue[1]);
    if (retval != 1) return retval;
    return genieWriteObj(GENIE_OBJ_ILED_DIGITS_L, index, frame.wordValue[0]);
}

int GeniePi::_genieWriteContrast(int value)
{
    unsigned char checksum;

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_WRITE_CONTRAST); checksum  = GENIE_WRITE_CONTRAST;
    geniePutchar(value);                checksum ^= (unsigned char)value;
    geniePutchar(checksum);

    while (!genieAck.load() && !genieNak.load())
        delay(1);

    return 0;
}

int GeniePi::genieWriteContrast(int value)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteContrast(value);
}

int GeniePi::_genieWriteStr(int index, char *string)
{
    unsigned char checksum;
    int len = (int)strlen(string);

    if (len > 255)
        return -1;

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_WRITE_STR); checksum  = GENIE_WRITE_STR;
    geniePutchar(index);           checksum ^= (unsigned char)index;
    geniePutchar((unsigned char)len); checksum ^= (unsigned char)len;
    for (char *p = string; *p; ++p)
    {
        geniePutchar(*p);
        checksum ^= (unsigned char)*p;
    }
    geniePutchar(checksum);

    while (!genieAck.load() && !genieNak.load())
        delay(1);

    return 0;
}

int GeniePi::genieWriteStr(int index, char *string)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteStr(index, string);
}

int GeniePi::_genieWriteStrU(int index, char *string)
{
    unsigned char checksum;
    int len = (int)strlen(string);

    if (len > 255)
        return -1;

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_WRITE_STRU); checksum  = GENIE_WRITE_STRU;
    geniePutchar(index);            checksum ^= (unsigned char)index;
    geniePutchar((unsigned char)len); checksum ^= (unsigned char)len;
    for (char *p = string; *p; ++p)
    {
        geniePutchar((*p) >> 8);   checksum ^= (unsigned char)((*p) >> 8);
        geniePutchar((*p) & 0xFF); checksum ^= (unsigned char)((*p) & 0xFF);
    }
    geniePutchar(checksum);

    while (!genieAck.load() && !genieNak.load())
        delay(1);

    return 0;
}

int GeniePi::genieWriteStrU(int index, char *string)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteStrU(index, string);
}

int GeniePi::_genieMakeStr(int index, long n, int base)
{
    char buf[8 * sizeof(long) + 1];
    char *str = &buf[sizeof(buf) - 1];
    int neg = 0;
    if (n < 0) neg = 1;
    n = std::labs(n);

    *str = '\0';
    do {
        unsigned long m = n;
        n /= base;
        char c = (char)(m - base * n);
        *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while (n);
    if (neg) *--str = '-';

    _genieWriteStr(index, str);
    return 0;
}

int GeniePi::genieWriteStrHex(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeStr(index, n, 16);
}
int GeniePi::genieWriteStrOct(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeStr(index, n, 8);
}
int GeniePi::genieWriteStrBin(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeStr(index, n, 2);
}
int GeniePi::genieWriteStrBase(int index, long n, int base)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeStr(index, n, base);
}
int GeniePi::genieWriteStrDec(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeStr(index, n, 10);
}

int GeniePi::_genieWriteStrFloat(int index, float n, int precision)
{
    char str[64];
    std::snprintf(str, sizeof(str), "%.*g", precision, (double)n);
    _genieWriteStr(index, str);
    return 0;
}

int GeniePi::genieWriteStrFloat(int index, float n, int precision)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteStrFloat(index, n, precision);
}

int GeniePi::genieWriteInhLabelDefault(int index)
{
    return genieWriteObj(GENIE_OBJ_ILABELB, index, (unsigned int)-1);
}

int GeniePi::_genieWriteInhLabel(int index, char *string)
{
    unsigned char checksum;
    int len = (int)strlen(string);

    if (len > 255)
        return -1;

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_WRITE_INH_LABEL); checksum  = GENIE_WRITE_INH_LABEL;
    geniePutchar(index);                 checksum ^= (unsigned char)index;
    geniePutchar((unsigned char)len);    checksum ^= (unsigned char)len;
    for (char *p = string; *p; ++p)
    {
        geniePutchar(*p);
        checksum ^= (unsigned char)*p;
    }
    geniePutchar(checksum);

    while (!genieAck.load() && !genieNak.load())
        delay(1);

    return 0;
}

int GeniePi::genieWriteInhLabel(int index, char *string)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteInhLabel(index, string);
}

int GeniePi::_genieMakeInhLabel(int index, long n, int base)
{
    char buf[8 * sizeof(long) + 1];
    char *str = &buf[sizeof(buf) - 1];
    int neg = 0;
    if (n < 0) neg = 1;
    n = std::labs(n);

    *str = '\0';
    do {
        unsigned long m = n;
        n /= base;
        char c = (char)(m - base * n);
        *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while (n);
    if (neg) *--str = '-';

    _genieWriteInhLabel(index, str);
    return 0;
}

int GeniePi::genieWriteInhLabelHex(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeInhLabel(index, n, 16);
}
int GeniePi::genieWriteInhLabelOct(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeInhLabel(index, n, 8);
}
int GeniePi::genieWriteInhLabelBin(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeInhLabel(index, n, 2);
}
int GeniePi::genieWriteInhLabelBase(int index, long n, int base)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeInhLabel(index, n, base);
}
int GeniePi::genieWriteInhLabelDec(int index, long n)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieMakeInhLabel(index, n, 10);
}

int GeniePi::_genieWriteInhLabelFloat(int index, float n, int precision)
{
    char str[64];
    std::snprintf(str, sizeof(str), "%.*g", precision, (double)n);
    _genieWriteInhLabel(index, str);
    return 0;
}

int GeniePi::genieWriteInhLabelFloat(int index, float n, int precision)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteInhLabelFloat(index, n, precision);
}

int GeniePi::_genieWriteMagicBytes(int magic_index, unsigned int *byteArray)
{
    unsigned int *p;
    unsigned char checksum;
    int len = 0;

    // byteArray null (0) ile biten bir dizi; gercek eleman sayisini
    // pointer boyutundan degil, diziyi gezerek buluyoruz.
    for (p = byteArray; *p; ++p)
        ++len;

    if (len > 255)
        return -1;

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_MAGIC_BYTES); checksum  = GENIE_MAGIC_BYTES;
    geniePutchar(magic_index);       checksum ^= (unsigned char)magic_index;
    geniePutchar((unsigned char)len); checksum ^= (unsigned char)len;
    for (p = byteArray; *p; ++p)
    {
        geniePutchar((int)*p);
        checksum ^= (unsigned char)*p;
    }
    geniePutchar(checksum);

    while (!genieAck.load() && !genieNak.load())
        delay(1);

    return 0;
}

int GeniePi::genieWriteMagicBytes(int magic_index, unsigned int *byteArray)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteMagicBytes(magic_index, byteArray);
}

int GeniePi::_genieWriteDoubleBytes(int magic_index, unsigned int *doubleByteArray)
{
    unsigned int *p;
    unsigned char checksum;
    int len = 0;

    // doubleByteArray null (0) ile biten bir dizi; gercek eleman sayisini
    // pointer boyutundan degil, diziyi gezerek buluyoruz.
    for (p = doubleByteArray; *p; ++p)
        ++len;

    if (len > 255)
        return -1;

    genieAck.store(false);
    genieNak.store(false);

    geniePutchar(GENIE_DOUBLE_BYTES); checksum  = GENIE_MAGIC_BYTES;
    geniePutchar(magic_index);        checksum ^= (unsigned char)magic_index;
    geniePutchar((unsigned char)len); checksum ^= (unsigned char)len;
    for (p = doubleByteArray; *p; ++p)
    {
        unsigned char hi = (unsigned char)((*p) >> 8);
        unsigned char lo = (unsigned char)((*p) & 0xFF);
        geniePutchar(hi); checksum ^= hi;
        geniePutchar(lo); checksum ^= lo;
    }
    geniePutchar(checksum);

    while (!genieAck.load() && !genieNak.load())
        delay(1);

    return 0;
}

int GeniePi::genieWriteDoubleBytes(int magic_index, unsigned int *doubleByteArray)
{
    std::lock_guard<std::mutex> lock(genieMutex);
    return _genieWriteDoubleBytes(magic_index, doubleByteArray);
}

int GeniePi::genieSetup(char *device, int baud)
{
    struct timeval tv;

    if ((genieFd = genieOpen(device, baud)) < 0)
        return -1;

    genieFlush(genieFd);

    gettimeofday(&tv, NULL);
    epoch = (tv.tv_sec * 1000000ULL + tv.tv_usec) / 1000;

    for (int i = 0; i < 10; ++i)
    {
        geniePutchar('X');
        if (genieGetchar() == GENIE_NAK)
            break;
    }

    listenerRunning.store(true);
    listenerThread = std::thread(&GeniePi::genieReplyListener, this);

    return 0;
}
