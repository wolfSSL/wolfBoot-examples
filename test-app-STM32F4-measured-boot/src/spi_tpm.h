/* tpm_spi.h
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

/** SPI settings for TPM2.0 module **/

#undef SPI_GPIO
#define SPI_GPIO    GPIOB_BASE

#undef SPI_CS_GPIO
#define SPI_CS_GPIO GPIOE_BASE

#undef SPI_CS_TPM
#define SPI_CS_TPM   0 /* TPM CS connected to GPIOE0 */

#undef SPI1_CLOCK_PIN
#define SPI1_CLOCK_PIN 3 /* SPI_SCK: PB3  */

#undef SPI1_MISO_PIN
#define SPI1_MISO_PIN  4 /* SPI_MISO PB4  */

#undef SPI1_MOSI_PIN
#define SPI1_MOSI_PIN  5 /* SPI_MOSI PB5  */
