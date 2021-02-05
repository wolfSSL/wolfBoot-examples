# For nRF51-based targets, we need to reduce buffer sizes to make this test fit
# into RAM
# Note: as the CPU variable is not set at this point, we manually 'whitelist'
#       all supported nrf51-boards here

# Set the tests default configuration
APP_MTU ?= 5000
APP_BUF_CHUNKSIZE ?= 250    # must be full divider of APP_MTU
APP_BUF_NUM ?= 3
APP_CID ?= 0x0235

# Apply configuration values
CFLAGS += -DAPP_MTU=$(APP_MTU)
CFLAGS += -DAPP_BUF_CHUNKSIZE=$(APP_BUF_CHUNKSIZE)
CFLAGS += -DAPP_BUF_NUM=$(APP_BUF_NUM)
CFLAGS += -DAPP_NODENAME=$(APP_NODENAME)
CFLAGS += -DAPP_CID=$(APP_CID)

# configure NimBLE
MSYS_CNT ?= 40

