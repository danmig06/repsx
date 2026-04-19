#ifndef PSX_INPUT_H
#define PSX_INPUT_H

struct sio_dev {
	uint8_t id;
	bool (*send)(struct sio_dev*, uint8_t);
	uint8_t (*recv)(struct sio_dev*);
	void (*reset)(struct sio_dev*);
	bool (*tx_finished)(struct sio_dev*);
};

#endif // #ifndef PSX_INPUT_H
