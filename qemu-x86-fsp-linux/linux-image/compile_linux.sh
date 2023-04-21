#!/bin/sh

WORK_DIR=/tmp/br-linux-wolfboot
BR_VER=2022.08.3
BR_DIR=buildroot-$BR_VER
IMAGE_DIR=$WORK_DIR/output

if (test ! -d $WORK_DIR);then
    mkdir -p $WORK_DIR
fi

if (test ! -d $WORK_DIR/$BR_DIR);then
    curl https://buildroot.org/downloads/$BR_DIR.tar.gz -o $WORK_DIR/$BR_DIR.tar.gz
    tar xvf $WORK_DIR/$BR_DIR.tar.gz -C $WORK_DIR
fi

BR2_EXTERNAL=$(pwd)/br_ext_dir make -C $WORK_DIR/$BR_DIR tiny_defconfig O=$IMAGE_DIR
make -C $WORK_DIR/$BR_DIR O=$IMAGE_DIR
cp $IMAGE_DIR/images/bzImage ./app.bin

