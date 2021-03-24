/*
*****************************************************************************
* Copyright by ams AG                                                       *
* All rights are reserved.                                                  *
*                                                                           *
* IMPORTANT - PLEASE READ CAREFULLY BEFORE COPYING, INSTALLING OR USING     *
* THE SOFTWARE.                                                             *
*                                                                           *
* THIS SOFTWARE IS PROVIDED FOR USE ONLY IN CONJUNCTION WITH AMS PRODUCTS.  *
* USE OF THE SOFTWARE IN CONJUNCTION WITH NON-AMS-PRODUCTS IS EXPLICITLY    *
* EXCLUDED.                                                                 *
*                                                                           *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS       *
* "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT         *
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS         *
* FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT  *
* OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,     *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT          *
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     *
* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY     *
* THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT       *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE     *
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.      *
*****************************************************************************
*/

/***** tof8801_bootloader.c *****/
#include "tof8801.h"
#include "tof8801_driver.h"

const char *tof8801_bootloader_cmd_stat_str[MAX_BL_CMD_STAT] = {
  "READY"         ,
  "ERR_SIZE"      ,
  "ERR_CSUM"      ,
  "ERR_RES"       ,
  "ERR_APP"       ,
  "ERR_TIMEOUT"   ,
  "ERR_LOCK"      ,
  "ERR_RANGE"     ,
  "ERR_MORE"      ,
  "ERROR1"        ,
  "ERROR2"        ,
  "ERROR3"        ,
  "ERROR4"        ,
  "ERROR5"        ,
  "ERROR6"        ,
  "ERROR7"        ,
  "CMD_BUSY"      ,
};

void tof8801_BL_init_app(struct tof8801_BL_application *BL_app)
{
  //Wipe all local settings for bootloader, (except app_id)
  memset(BL_app, 0, sizeof(struct tof8801_BL_application));
  BL_app->app_id = TOF8801_APP_ID_BOOTLOADER;
}

/**
 * is_BL_cmd_busy - return non-zero if BL cmd is busy
 *
 * @app: pointer to bootloder application struct
 */
int is_BL_cmd_busy(struct i2c_client *client)
{
  char status = CMD_BUSY;
  tof8801_get_register(client, TOF8801_CMD_STAT, &status);
  return TOF8801_BL_IS_CMD_BUSY(status);
}

/**
 * get_BL_cmd_buf - return pointer to raw cmd buffer
 *
 * @app: pointer to bootloder application struct
 */
char * get_BL_cmd_buf(struct tof8801_BL_application *app)
{
  return app->BL_command.anon_cmd.data;
}

/**
 * get_BL_rsp_buf - return pointer to raw rsp buffer
 *
 * @app: pointer to bootloder application struct
 */
char * get_BL_rsp_buf(struct tof8801_BL_application *app)
{
  return app->BL_response.anon_resp.data;
}

/**
 * tof8801_BL_read_status - Retry until cmd is executed and status is
 *                          available in local buffer
 *
 * @client: pointer to i2c_client
 * @BL_app: pointer to BL application struct
 * @num_retries: how many times to retry to retrieve status
 */
int tof8801_BL_read_status(struct i2c_client *client,
                           struct tof8801_BL_application *BL_app,
                           int num_retries)
{
  int error = 0;
  char *rbuf = get_BL_rsp_buf(BL_app);
  char *status = &BL_app->BL_response.short_resp.status;
  char *rdata_size = &BL_app->BL_response.short_resp.size;
  char chksum;
  if (num_retries < 0)
    num_retries = 5;
  do {
    num_retries -= 1;
    error = tof_i2c_read(client, TOF8801_CMD_STAT,
                         rbuf, TOF8801_I2C_HEADER_SIZE);
    if (error)
      continue;
    if (TOF8801_BL_IS_CMD_BUSY(*status)) {
      /* CMD is still executing, wait and retry */
      udelay(TOF8801_BL_CMD_WAIT_MSEC*1000);
      if (num_retries <= 0) {
        dev_info(&client->dev, "BL application is busy: %#04x", *status);
        error = -EBUSY;
      }
      continue;
    }
    /* if we have reached here, the command has either succeeded or failed */
    if ( *rdata_size >= 0 ) {
      /* read in data part and csum */
      error = tof_i2c_read(client, TOF8801_CMD_STAT,
                           rbuf, TOF8801_CALC_BL_RSP_SIZE(*rdata_size));
      if (error)
        continue;
      chksum = (char) ~tof8801_calc_chksum(rbuf, TOF8801_CALC_BL_RSP_SIZE(*rdata_size));
      if ((chksum != TOF_VALID_CHKSUM) && (num_retries <= 0)) {
        dev_err(&client->dev,
                "Checksum verification of Response failed: %#04x", chksum);
        return -EIO;
      }
      /* all done, break and return */
      break;
    }
  } while ((error == 0) && (num_retries > 0));
  dev_dbg(&client->dev, "BL application wait for response: \'%d\'", error);
  return error;
}

/**
 * tof8801_calc_chksum - calculate the chksum and return the result
 *
 * @data: pointer to data to chksum
 * @size: number of elements to chksum over in data
 */
char tof8801_calc_chksum(const char *data, char size)
{
  unsigned int sum = 0;
  int idx = 0;
  for (; idx < size; idx++) {
    sum += data[idx];
  }
  return (char) ~sum; /* 1's complement of lowest byte */
}

/**
 * tof8801_BL_send_rcv_cmd - Send a command and wait for response
 *
 * @client: pointer to i2c_client
 * @BL_app: pointer to BL application struct, result will be in BL_response
 */
int tof8801_BL_send_rcv_cmd(struct i2c_client *client, struct tof8801_BL_application *BL_app)
{
  int error = -1;
  char *wbuf = get_BL_cmd_buf(BL_app);
  char wsize = TOF8801_CALC_BL_CMD_SIZE(wbuf[1]);
  if (is_BL_cmd_busy(client))
    return error;

  error = tof_i2c_write(client, TOF8801_CMD_STAT, wbuf, wsize);
  if (error)
    return error;

  return tof8801_BL_read_status(client, BL_app, 5);
}

/**
 * tof8801_BL_send_cmd - Send a command and do not wait for response
 *                           This is primarily used for commands that reset
 *                           the chip (RESET, RAM REMAP, etc)
 *
 * @client: pointer to i2c_client
 * @BL_app: pointer to BL application struct
 */
int tof8801_BL_send_cmd(struct i2c_client *client, struct tof8801_BL_application *BL_app)
{
  int error = -1;
  char *wbuf = get_BL_cmd_buf(BL_app);
  char wsize = TOF8801_CALC_BL_CMD_SIZE(wbuf[1]);
  if (is_BL_cmd_busy(client))
    return error;

  error = tof_i2c_write(client, TOF8801_CMD_STAT, wbuf, wsize);
  if (error)
    return error;

  return error;
}

/**
 * tof8801_BL_short_cmd - Send a generic short cmd (no response expected)
 *                        to the BL app
 *
 * @client: pointer to i2c_client (struct i2c_client **)
 * @BL_app: pointer to BL application struct
 */
int tof8801_BL_short_cmd(struct i2c_client *client,
                         struct tof8801_BL_application *BL_app,
                         enum tof8801_bootloader_cmd cmd_e)
{
  struct tof8801_BL_short_cmd *cmd = &(BL_app->BL_command.short_cmd);
  cmd->command = cmd_e;
  cmd->size = 0;
  cmd->chksum = tof8801_calc_chksum(get_BL_cmd_buf(BL_app),
                                    TOF8801_CALC_CHKSUM_SIZE(cmd->size));

  return tof8801_BL_send_rcv_cmd(client, BL_app);
}

/**
 * tof8801_BL_reset - Send a soft reset cmd to the bootloader
 *
 * @BL_app: pointer to BL application struct
 */
int tof8801_BL_reset(struct i2c_client *client,
                     struct tof8801_BL_application *BL_app)
{
  int error;
  struct tof8801_BL_short_cmd *cmd = &(BL_app->BL_command.short_cmd);
  cmd->command = BL_RESET;
  cmd->size = 0;
  cmd->chksum = tof8801_calc_chksum(get_BL_cmd_buf(BL_app),
                                    TOF8801_CALC_CHKSUM_SIZE(cmd->size));

  error = tof8801_BL_send_cmd(client, BL_app);
  if (error)
    return error;
  return tof_wait_for_cpu_ready(client);
}

/**
 * tof8801_BL_ram_remap - Send a ram remap cmd to the bootloader
 *
 * @BL_app: pointer to BL application struct
 */
int tof8801_BL_ram_remap(struct i2c_client *client,
                         struct tof8801_BL_application *BL_app)
{
  int error;
  struct tof8801_BL_short_cmd *cmd = &(BL_app->BL_command.short_cmd);
  cmd->command = BL_RAMREMAP_RESET;
  cmd->size = 0;
  cmd->chksum = tof8801_calc_chksum(get_BL_cmd_buf(BL_app),
                                    TOF8801_CALC_CHKSUM_SIZE(cmd->size));

  error = tof8801_BL_send_cmd(client, BL_app);
  if (error)
    return error;
  return tof_wait_for_cpu_startup(client);
}

/**
 * tof8801_BL_addr_ram - Set RAM pointer in the bootloader
 *
 * @client: pointer to i2c_client
 * @BL_app: pointer to BL application struct
 */
int tof8801_BL_addr_ram(struct i2c_client *client,
                        struct tof8801_BL_application *BL_app,
                        int addr)
{
  struct tof8801_BL_addr_ram_cmd *cmd = &(BL_app->BL_command.addr_ram_cmd);
  cmd->command = BL_ADDR_RAM;
  cmd->size = 2;
  cmd->addr_lsb  = addr & 0xff;
  cmd->addr_msb  = (addr >> 8) & 0xff;
  cmd->chksum = tof8801_calc_chksum(get_BL_cmd_buf(BL_app),
                                    TOF8801_CALC_CHKSUM_SIZE(cmd->size));

  return tof8801_BL_send_rcv_cmd(client, BL_app);
}

/**
 * tof8801_BL_read_ram - Read from RAM a specific number of bytes
 *
 * @client: pointer to i2c_client
 * @BL_app: pointer to BL application struct
 * @num_bytes: number of bytes to read
 * @rbuf: location to put read RAM values into,
 *        *** must be big enough to hold 'num_bytes' Bytes ***
 */
int tof8801_BL_read_ram(struct i2c_client *client,
                        struct tof8801_BL_application *BL_app,
                        char * rbuf, int len)
{
  int error = 0;
  int rc;
  struct tof8801_BL_read_ram_cmd *cmd = &(BL_app->BL_command.read_ram_cmd);
  struct tof8801_BL_read_ram_resp *rsp = &(BL_app->BL_response.read_ram_resp);
  int num = 0;
  do {
    cmd->command = BL_R_RAM;
    cmd->size = 1;
    cmd->num_bytes = ((len - num) >= TOF8801_I2C_MAX_DATA_SIZE) ?
                     TOF8801_I2C_MAX_DATA_SIZE : (char) (len - num);
    cmd->chksum = tof8801_calc_chksum(get_BL_cmd_buf(BL_app),
                                      TOF8801_CALC_CHKSUM_SIZE(cmd->size));
    rc = tof8801_BL_send_rcv_cmd(client, BL_app);
    if (!rc) {
      /* command was successful, lets copy a batch of data over */
      if (rbuf)
        memcpy((rbuf + num), rsp->data, rsp->size);
      num += cmd->num_bytes;
    }
    error = error ? error : (rc ? rc : 0);
  } while ((num < len) && !rc);
  return error;
}

/**
 * tof8801_BL_write_ram - write to RAM a specific number of bytes
 *
 * @client: pointer to i2c_client
 * @BL_app: pointer to BL application struct
 * @buf: pointer to buffer of bytes to write
 * @len: number of bytes to write, capped at 127 (0x80)
 */
int tof8801_BL_write_ram(struct i2c_client *client,
                         struct tof8801_BL_application *BL_app,
                         const char *buf, int len)
{
  struct tof8801_BL_write_ram_cmd *cmd = &(BL_app->BL_command.write_ram_cmd);
  int idx = 0;
  int num = 0;
  char chunk_bytes = 0;
  int error = 0;
  int rc;
  do {
    cmd->command = BL_W_RAM;
    chunk_bytes = ((len - num) > TOF8801_I2C_MAX_DATA_SIZE) ?
                  TOF8801_I2C_MAX_DATA_SIZE : (char) (len - num);
    cmd->size = chunk_bytes;
    for(idx = 0; idx < cmd->size; idx++) {
      cmd->data[idx] = buf[num + idx];
    }
    /* add chksum to end */
    cmd->data[(unsigned char)cmd->size] =
      tof8801_calc_chksum(get_BL_cmd_buf(BL_app),
                          TOF8801_CALC_CHKSUM_SIZE(cmd->size));
    rc = tof8801_BL_send_rcv_cmd(client, BL_app);
    if (!rc)
      num += chunk_bytes;
    error = error ? error : (rc ? rc : 0);
  } while ((num < len) && !rc);
  return error;
}

/**
 * tof8801_BL_upload_init - Initialize the salt value for encrypted downloads
 *
 * @client: pointer to i2c_client
 * @BL_app: pointer to BL application struct
 * @salt:   salt value to set for encrypted upload
 */
int tof8801_BL_upload_init(struct i2c_client *client,
                           struct tof8801_BL_application *BL_app,
                           char salt)
{
  struct tof8801_BL_upload_init_cmd *cmd = &(BL_app->BL_command.upload_init_cmd);
  cmd->command = BL_UPLOAD_INIT;
  cmd->size = 1;
  cmd->seed = salt;
  cmd->chksum = tof8801_calc_chksum(get_BL_cmd_buf(BL_app),
                                    TOF8801_CALC_CHKSUM_SIZE(cmd->size));

  return tof8801_BL_send_rcv_cmd(client, BL_app);
}

