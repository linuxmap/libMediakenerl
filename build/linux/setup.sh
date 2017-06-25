#!/bin/bash
#set -x
set -o nounset
MK_VERSION="mediakernel 1.0.1"
CURRENT_PATH=`pwd`
cd ${CURRENT_PATH}/../..
export MK_ROOT=$PWD
export USR_ROOT=/usr/local/
export THIRD_ROOT=${CURRENT_PATH}/3rd_party/
export PATCH_ROOT=${CURRENT_PATH}/patch/
export SCRIPT_ROOT=${CURRENT_PATH}/script/
export PREFIX_ROOT=${USR_ROOT}
echo "------------------------------------------------------------------------------"
echo " MK_ROOT exported as ${MK_ROOT}"
echo "------------------------------------------------------------------------------"

build_ffmpeg()
{
    module_pack="ffmpeg-3.2.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the ffmpeg package from server\n"
        wget http://ffmpeg.org/releases/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd ffmpeg*
    patch -p0 <${PATCH_ROOT}/ffmpeg_hlsen_c.patch
    patch -p0 <${PATCH_ROOT}/ffmpeg_rtsp_c.patch
    patch -p0 <${PATCH_ROOT}/ffmpeg_rtsp_h.patch
    patch -p0 <${PATCH_ROOT}/ffmpeg_http_c.patch
    sed -i 's/^#define LIBAVCODEC_IDENT*$/#define LIBAVCODEC_IDENT ${MK_VERSION} /g' ./libavcodec/version.h 
    sed -i 's/^#define LIBAVDEVICE_IDENT*$/#define LIBAVDEVICE_IDENT ${MK_VERSION} /g' ./libavdevice/version.h
    sed -i 's/^#define LIBAVFILTER_IDENT*$/#define LIBAVFILTER_IDENT ${MK_VERSION} /g' ./libavfilter/version.h
    sed -i 's/^#define LIBAVFORMAT_IDENT*$/#define LIBAVFORMAT_IDENT ${MK_VERSION} /g' ./libavformat/version.h
    sed -i 's/^#define LIBAVRESAMPLE_IDENT*$/#define LIBAVRESAMPLE_IDENT ${MK_VERSION} /g' ./libavresample/version.h
    sed -i 's/^#define LIBAVUTIL_IDENT*$/#define LIBAVUTIL_IDENT ${MK_VERSION} /g' ./libavutil/version.h
    sed -i 's/^#define LIBPOSTPROC_IDENT*$/#define LIBPOSTPROC_IDENT ${MK_VERSION} /g' ./libpostproc/version.h
    sed -i 's/^#define LIBSWRESAMPLE_IDENT*$/#define LIBSWRESAMPLE_IDENT ${MK_VERSION} /g' ./libswresample/version.h
    sed -i 's/^#define LIBSWSCALE_IDENT*$/#define LIBSWSCALE_IDENT ${MK_VERSION} /g' ./libswscale/version.h

    ./configure --prefix=${PREFIX_ROOT} \
                --enable-shared         \
                --disable-ffplay        \
                --disable-ffprobe       \
                --disable-ffserver      \
                --disable-yasm          \
                --enable-pic            \
                --enable-libx264        \
                --enable-gpl            \
                --enable-pthreads       \
                --enable-nonfree        \
                --extra-cflags=-I${PREFIX_ROOT}/include --extra-ldflags=-L/${PREFIX_ROOT}/lib
                
    if [ 0 -ne ${?} ]; then
        echo "configure ffmpeg fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build ffmpeg fail!\n"
        return 1
    fi
    
    #change the libavutil/time.h to libavutil/avtime.h
    
    mv ${PREFIX_ROOT}/include/libavutil/time.h ${PREFIX_ROOT}/include/libavutil/avtime.h
    return 0
}

build_x264()
{
    module_pack="last_x264.tar.bz2"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the x264 package from server\n"
        wget ftp://ftp.videolan.org/pub/x264/snapshots/${module_pack}
    fi
    tar -jxvf ${module_pack}
    
    cd x264*
    ./configure --prefix=${PREFIX_ROOT} \
                --disable-asm \
                --enable-static \
                --enable-pic \
                --disable-opencl  \
                --disable-avs \
                --disable-cli \
                --disable-ffms \
                --disable-gpac  \
                --disable-lavf  \
                --disable-swscale 
    if [ 0 -ne ${?} ]; then
        echo "configure x264 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build x264 fail!\n"
        return 1
    fi
    
    return 0
}

build_ffad()
{
    module_pack="faad2-2.7.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the faad2 package from server\n"
        wget http://downloads.sourceforge.net/faac/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd faad2*
    ./configure --prefix=${PREFIX_ROOT} 
    if [ 0 -ne ${?} ]; then
        echo "configure faad2 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build faad2 fail!\n"
        return 1
    fi
    
    return 0
}

build_faac()
{
    module_pack="faac-1.28.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the faac-1 package from server\n"
        wget http://downloads.sourceforge.net/faac/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd faac*
    patch -p0 <${PATCH_ROOT}/faac.patch
    ./configure --prefix=${PREFIX_ROOT} 
    if [ 0 -ne ${?} ]; then
        echo "configure faac fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build faac fail!\n"
        return 1
    fi
    
    return 0
}

build_lame()
{
    module_pack="lame-3.99.5.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the lame package from server\n"
        wget https://sourceforge.net/projects/lame/files/lame/3.99/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd lame*
    ./configure --prefix=${PREFIX_ROOT} 
    if [ 0 -ne ${?} ]; then
        echo "configure lame fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build lame fail!\n"
        return 1
    fi
    
    return 0
}

build_pcre()
{
    module_pack="pcre-8.39.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the pcre package from server\n"
        wget https://sourceforge.net/projects/pcre/files/pcre/8.39/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd pcre*
    ./configure --prefix=${PREFIX_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure pcre fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build pcre fail!\n"
        return 1
    fi
    
    return 0
}

build_zlib()
{
    module_pack="zlib-1.2.8.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the zlib package from server\n"
        wget http://zlib.net/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd zlib*
    ./configure --prefix=${PREFIX_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure zlib fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build zlib fail!\n"
        return 1
    fi
    
    return 0
}

build_libiconv()
{
    module_pack="libiconv-1.14.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libiconv package from server\n"
        wget http://ftp.gnu.org/pub/gnu/libiconv/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libiconv*
    patch -p0 <${PATCH_ROOT}/libiconv.patch
    ./configure --prefix=${PREFIX_ROOT} --enable-static=yes
                
    if [ 0 -ne ${?} ]; then
        echo "configure libiconv fail!\n"
        return 1
    fi
    
    make clean  
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libiconv fail!\n"
        return 1
    fi
    
    return 0
}

build_xzutils()
{
    module_pack="xz-5.2.2.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the xzutils package from server\n"
        wget http://tukaani.org/xz/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd xz*
    ./configure --prefix=${PREFIX_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure xzutils fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build xzutils fail!\n"
        return 1
    fi
    
    return 0
}

build_bzip2()
{
    module_pack="bzip2-1.0.6.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the bzip2 package from server\n"
        wget http://www.bzip.org/1.0.6/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd bzip2*
    #./configure --prefix=${PREFIX_ROOT}
    PREFIX_ROOT_SED=$(echo ${PREFIX_ROOT} |sed -e 's/\//\\\//g')
    sed -i "s/PREFIX\=\/usr\/local/PREFIX\=${PREFIX_ROOT_SED}/" Makefile    
    sed -i "s/CFLAGS=-Wall -Winline -O2 -g/CFLAGS\=-Wall -Winline -O2 -fPIC -g/" Makefile
    if [ 0 -ne ${?} ]; then
        echo "sed bzip2 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build bzip2 fail!\n"
        return 1
    fi
    
    return 0
}


build_extend_modules()
{
    build_bzip2
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_zlib
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_pcre
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libiconv
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_lame
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_faac
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_ffad
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_x264
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_xzutils
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_ffmpeg
    if [ 0 -ne ${?} ]; then
        return 1
    fi

    return 0
}

build_mk_module()
{
    cd ${MK_ROOT}/build/linux/
    mkdir lib/
    make -f Makefile prefix=${PREFIX_ROOT} with-ffmpeg=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "build the media kernel module fail!\n"
        return 1
    fi
    
    make install -f Makefile prefix=${PREFIX_ROOT} with-ffmpeg=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "install the media kernel module fail!\n"
        return 1
    fi

    return 0
}

setup()
{  
    build_extend_modules
    if [ 0 -ne ${?} ]; then
        return
    fi 
    build_mk_module
    if [ 0 -ne ${?} ]; then
        return
    fi
    echo "make the all modules success!\n"
    cd ${MK_ROOT}
}

show_help()
{
    echo "-h: show the help info"
    echo "-p: set the install path"
    echo " e.g.: $0 -p /usr/local/" 
}

if [ $# == 0 ]; then
    echo "build and install to ${PREFIX_ROOT}"
    setup
    exit
fi

while getopts "help:prefix" opt; do  
  case $opt in  
    h)  
      show_help   
      ;;  
    p)  
      export PREFIX_ROOT=${OPTARG}  
      setup      
      ;;    
    ?)  
      echo "Invalid option: ${OPTARG}"   
      ;;  
  esac  
done  
