/* i2c.h - bit-banged multi-port I2C master for BWX468.
 *
 * Reconstructed from the stock i2c_* functions. SCL is a single shared line;
 * each logical port (USB-C port / PD controller) has its own SDA line and 7-bit
 * device address, described by the board's i2c_port_cfg_t table.
 *
 * Timing matches stock: bit delays use i2c_delay(6) / i2c_delay(3).
 */
#ifndef HAL_I2C_H
#define HAL_I2C_H
#include <stdint.h>

typedef struct {
    uint8_t  sda_port;   /* GPIO port carrying SDA            */
    uint16_t sda_mask;   /* GPIO pin mask for SDA             */
    uint8_t  addr7;      /* 7-bit I2C address of the device   */
} i2c_port_cfg_t;

/* Board provides the table and its length (see board/board_config.c). */
extern const i2c_port_cfg_t g_i2c_ports[];
extern const uint8_t        g_i2c_port_count;

/* Shared SCL line (stock: GPIO port 2, mask 0x2000). */
#define I2C_SCL_PORT   2u
#define I2C_SCL_MASK   0x2000u

void i2c_init(void);
void i2c_scan(uint8_t port, volatile unsigned char *out);
void i2c_select_port(uint8_t port);   /* set active port for following ops  */

void i2c_start(void);
void i2c_stop(void);
int  i2c_write_byte(uint8_t val);     /* returns 0 = ACK, -1 = NACK          */
uint8_t i2c_read_byte(void);          /* reads 8 bits (SDA as input)         */
void i2c_master_ack(int nack);        /* drive ACK(0)/NACK(1) to slave       */

/* Read a little-endian 32-bit register from the device on `port`.
 * Faithful to stock pd_read_reg: returns 1 on success, 0 on bus error. */
int  i2c_read_reg32(uint8_t port, uint8_t reg, uint32_t *out);

/* Write to register `reg` (stock sub_171DC format: addr, reg, LEN, data[LEN]). */
int  i2c_write_reg32(uint8_t port, uint8_t reg, uint32_t value);
int  i2c_write_reg8(uint8_t port, uint8_t reg, uint8_t value);
int  i2c_write_reg(uint8_t port, uint8_t reg, const uint8_t *data, uint8_t len);

#endif /* HAL_I2C_H */
