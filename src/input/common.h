#ifndef PSX_INPUT_COMMON_H
#define PSX_INPUT_COMMON_H

#include <string.h>

struct sio_dev {
	uint8_t id;
	bool (*send)(struct sio_dev*, uint8_t);
	uint8_t (*recv)(struct sio_dev*);
	void (*reset)(struct sio_dev*);
	bool (*tx_finished)(struct sio_dev*);
};

#define SIO_EXTRA_SIZE(d) (sizeof(d) - sizeof(struct sio_dev))
#define SIO_MAX_EXTRA_SIZE 256 - sizeof(struct sio_dev)
struct sio_dev_base {
	struct sio_dev dev;
	char data[SIO_MAX_EXTRA_SIZE];
};

#define ASSERT_EXTRA_SIZE(T) \
	typedef char __static_assert_sizeof_##T[(SIO_EXTRA_SIZE(T) <= SIO_MAX_EXTRA_SIZE) ? 1 : -1]

inline void sio_dev_clear(struct sio_dev* device) {
	memset(device, 0, sizeof(struct sio_dev_base));
}

#endif // #ifndef PSX_INPUT_COMMON_H

