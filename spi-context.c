/*
 * generic SPI functions
 *
 * Copyright 2013, 2014 Zefir Kurtisi <zefir.kurtisi@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "spi-context.h"

#include "logging.h"
#include "miner.h"
#include "stu-device.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <unistd.h>

struct spi_ctx *spi_init(struct spi_config *config)
{
  char dev_fname[128];
  struct spi_ctx *ctx;

  if (config == NULL)
    return NULL;

  sprintf(dev_fname, SPI_DEVICE_TEMPLATE, config->bus, config->cs_line);

  int fd = open(dev_fname, O_RDWR);
  if (fd < 0) {
    applog(LOG_ERR, "SPI: Can not open SPI device %s", dev_fname);
    return NULL;
  }

  if ((ioctl(fd, SPI_IOC_WR_MODE, &config->mode) < 0) ||
      (ioctl(fd, SPI_IOC_RD_MODE, &config->mode) < 0) ||
      (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &config->bits) < 0) ||
      (ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &config->bits) < 0) ||
      (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &config->speed) < 0) ||
      (ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &config->speed) < 0)) {
    applog(LOG_ERR, "SPI: ioctl error on SPI device %s", dev_fname);
    close(fd);
    return NULL;
  }

  ctx = malloc(sizeof(*ctx));
  assert(ctx != NULL);
  ctx->devname = strdup(dev_fname);
  ctx->fd = fd;
  ctx->config = *config;
  applog(LOG_WARNING, "SPI '%s': mode=%hhu, bits=%hhu, speed=%u",
         dev_fname, ctx->config.mode, ctx->config.bits,
         ctx->config.speed);
  return ctx;
}

extern void spi_exit(struct spi_ctx *ctx)
{
	if (NULL == ctx)
		return;

	close(ctx->fd);
	free(ctx);
}

extern bool spi_transfer(struct spi_ctx *ctx, uint8_t *txbuf,
			 uint8_t *rxbuf, int len)
{
	struct spi_ioc_transfer xfr;
	int ret;

	if (rxbuf != NULL)
		memset(rxbuf, 0xff, len);

	ret = len;

	xfr.tx_buf = (unsigned long)txbuf;
	xfr.rx_buf = (unsigned long)rxbuf;
	xfr.len = len;
	xfr.speed_hz = ctx->config.speed;
	xfr.delay_usecs = ctx->config.delay;
	xfr.bits_per_word = ctx->config.bits;
	xfr.cs_change = 0;
	xfr.pad = 0;

	ret = ioctl(ctx->fd, SPI_IOC_MESSAGE(1), &xfr);
	if (ret < 1)
		applog(LOG_ERR, "SPI: ioctl error on SPI device: %d", ret);

	return ret > 0;
}

bool spi_write_data(struct spi_ctx *ctx, uint8_t *txbuf, int len)
{
	bool ret = true;

	if((len <= 0) || (txbuf == NULL))
	{
		applog(LOG_ERR, "SPI: write para error");
		ret = false;
	}
	
	if(write(ctx->fd, txbuf, len) <= 0)
	{
		applog(LOG_ERR, "SPI: write data error");
		ret = false;
	}

	return ret;
}

bool spi_read_data(struct spi_ctx *ctx, uint8_t *rxbuf, int len)
{
	bool ret = true;

	if((len <= 0) || (rxbuf == NULL))
	{
		applog(LOG_ERR, "SPI: read para error");
		ret = false;
	}

	if(read(ctx->fd, rxbuf, len) <= 0)
	{
		applog(LOG_ERR, "SPI: read data error");
		ret = false;
	}
	return ret;
}

bool spi_send_data(struct spi_ctx *ctx, uint8_t *txbuf, int len)
{
	bool ret;
	int index = 0;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];

	memset(spi_tx, 0, sizeof(spi_tx));
	memcpy(spi_tx, txbuf, len);

	do{
		ret = spi_write_data(ctx, spi_tx + index, 2);
		if(!ret)
		{
			return false;
		}		
		
		index = index + 2;
		usleep(5);
	}while(index < len);

	return true;
}