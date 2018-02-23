#!/bin/bash
#set -x
set -o nounset
MK_VERSION="mediakernel 1.0.1"
CURRENT_PATH=`pwd`
cd ${CURRENT_PATH}/../..
export MK_ROOT=$PWD
export USR_ROOT=${MK_ROOT}/extend/
export THIRD_ROOT=${CURRENT_PATH}/3rd_party/
export PATCH_ROOT=${CURRENT_PATH}/patch/
export PREFIX_ROOT=${USR_ROOT}
echo "------------------------------------------------------------------------------"
echo " MK_ROOT exported as ${MK_ROOT}"
echo "------------------------------------------------------------------------------"

build_ffmpeg()
{
    module_pack="ffmpeg-3.4.1.tar.gz"
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
    
    export PKG_CONFIG_PATH=${PREFIX_ROOT}/lib/pkgconfig/:${PKG_CONFIG_PATH}

    ./configure --prefix=${PREFIX_ROOT}    \
                --enable-shared            \
                --disable-ffplay           \
                --disable-ffprobe          \
                --disable-ffserver         \
                --disable-debug            \
                --disable-indev=sndio      \
                --disable-outdev=sndio     \
                --enable-x86asm            \
                --enable-pic               \
                --enable-gpl               \
                --enable-pthreads          \
                --enable-nonfree           \
                --enable-version3          \
                --enable-static            \
                --enable-frei0r            \
                --enable-gnutls            \
                --enable-gray              \
                --enable-libfreetype       \
                --enable-libmp3lame        \
                --enable-libopencore-amrnb \
                --enable-libopencore-amrwb \
                --enable-libopenjpeg       \
                --enable-librtmp           \
                --enable-libsoxr           \
                --enable-libspeex          \
                --enable-libvorbis         \
                --enable-libopus           \
                --enable-libtheora         \
                --enable-libvidstab        \
                --enable-libvpx            \
                --enable-libwebp           \
                --enable-libx264           \
                --enable-libx265           \
                --enable-libxvid           \
                --extra-cflags=-I${PREFIX_ROOT}/include \
                --extra-ldflags=-L/${PREFIX_ROOT}/lib   
                
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
build_libxml2()
{
    module_pack="libxml2-2.9.7.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libxml2 package from server\n"
        wget ftp://xmlsoft.org/libxml2/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libxml2*
    ./configure --prefix=${PREFIX_ROOT} --enable-shared=no --with-sax1 --with-zlib=${PREFIX_ROOT} --with-iconv=${PREFIX_ROOT}
                
    if [ 0 -ne ${?} ]; then
        echo "configure libxml2 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libxml2 fail!\n"
        return 1
    fi
    
    return 0
}

build_freetype()
{
    module_pack="freetype-2.6.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the freetype package from server\n"
        wget https://download.savannah.gnu.org/releases/freetype/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd freetype*
    
    ./configure --prefix=${PREFIX_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure freetype fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build freetype fail!\n"
        return 1
    fi
    
    return 0
}
build_fontconfig()
{
    module_pack="fontconfig-2.12.91.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the fontconfig package from server\n"
        wget https://www.freedesktop.org/software/fontconfig/release/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd fontconfig*
    
    export FREETYPE_CFLAGS=${PREFIX_ROOT}/include
    export FREETYPE_LIBS=${PREFIX_ROOT}/lib
    
    ./configure --prefix=${PREFIX_ROOT} --enable-iconv --enable-libxml2  --with-pkgconfigdir=${PREFIX_ROOT}/lib/
                
    if [ 0 -ne ${?} ]; then
        echo "configure fontconfig fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build fontconfig fail!\n"
        return 1
    fi
    
    return 0
}
build_frei0r_plugins()
{
    module_pack="frei0r-plugins-1.6.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the frei0r-plugins package from server\n"
        wget https://files.dyne.org/frei0r/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd frei0r-plugins*
    
    ./configure --prefix=${PREFIX_ROOT}
                
    if [ 0 -ne ${?} ]; then
        echo "configure frei0r-plugins fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build frei0r-plugins fail!\n"
        return 1
    fi
    
    return 0
}
build_fdkaac()
{
    module_pack="fdk-aac-v0.1.5.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the fdkaac package from server\n"
        wget https://github.com/mstorsjo/fdk-aac/archive/v0.1.5.tar.gz -O ${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd fdk-aac*
    
    ./autogen.sh
    
    ./configure --prefix=${PREFIX_ROOT} 
                
    if [ 0 -ne ${?} ]; then
        echo "configure fdk-aac fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build fdk-aac fail!\n"
        return 1
    fi
    
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
build_x265()
{
    module_pack="x265_2.6.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the x264 package from server\n"
        wget ftp://ftp.videolan.org/pub/videolan/x265/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd x265*/build/linux
    cmake -DCMAKE_INSTALL_PREFIX=${PREFIX_ROOT} -G "Unix Makefiles" ../../source

    if [ 0 -ne ${?} ]; then
        echo "configure x265 fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build x265 fail!\n"
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


build_opencore_amr()
{
    module_pack="opencore-amr-0.1.5.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the opencore-amr package from server\n"
        wget https://nchc.dl.sourceforge.net/project/opencore-amr/opencore-amr/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd opencore-amr*
    ./configure --prefix=${PREFIX_ROOT} 
    if [ 0 -ne ${?} ]; then
        echo "configure opencore-amr fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build opencore-amr fail!\n"
        return 1
    fi
    
    return 0
}

build_openjpeg()
{
    module_pack="openjpeg-v2.3.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the openjpeg package from server\n"
        wget https://github.com/uclouvain/openjpeg/archive/v2.3.0.tar.gz -O ${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd openjpeg*
    
    mkdir build
    
    cd build
    
    cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX_ROOT}
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build openjpeg fail!\n"
        return 1
    fi
    
    return 0
}

build_rubberband()
{
    module_pack="rubberband-v1.8.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the rubberband package from server\n"
        wget https://bitbucket.org/breakfastquay/rubberband/get/v1.8.1.tar.gz -O ${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd *rubberband*
    ./configure --prefix=${PREFIX_ROOT} 
    if [ 0 -ne ${?} ]; then
        echo "configure rubberband fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build rubberband fail!\n"
        return 1
    fi
    
    return 0
}
build_openssl()
{
    module_pack="openssl-0.9.8w.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the openssl package from server\n"
        wget https://www.openssl.org/source/old/0.9.x/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd openssl*
                    
    if [ 0 -ne ${?} ]; then
        echo "get openssl fail!\n"
        return 1
    fi
    
    ./config shared --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "config openssl fail!\n"
        return 1
    fi
    
    make
    if [ 0 -ne ${?} ]; then
        echo "make openssl fail!\n"
        return 1
    fi
    make test
    if [ 0 -ne ${?} ]; then
        echo "make test openssl fail!\n"
        return 1
    fi
    make install_sw
    if [ 0 -ne ${?} ]; then
        echo "make install openssl fail!\n"
        return 1
    fi
    
    return 0
}

build_rtmpdump()
{
    module_pack="rtmpdump-2.3.tgz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the rtmpdump package from server\n"
        wget http://rtmpdump.mplayerhq.hu/download/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd *rtmpdump*
    #./configure --prefix=${PREFIX_ROOT} 
    PREFIX_ROOT_SED=$(echo ${PREFIX_ROOT} |sed -e 's/\//\\\//g')
    sed -i "s/prefix\=\/usr\/local/prefix\=${PREFIX_ROOT_SED}/" Makefile 
    if [ 0 -ne ${?} ]; then
        echo "configure rtmpdump fail!\n"
        return 1
    fi
    
    sed -i "s/LIB_OPENSSL=-lssl -lcrypto/LIB_OPENSSL=-lssl -lcrypto -ldl/" Makefile 
    if [ 0 -ne ${?} ]; then
        echo "configure rtmpdump fail!\n"
        return 1
    fi
    
    sed -i "s/prefix\=\/usr\/local/prefix\=${PREFIX_ROOT_SED}/" ./librtmp/Makefile 
    if [ 0 -ne ${?} ]; then
        echo "configure librtmp fail!\n"
        return 1
    fi
    
    C_INCLUDE_PATH=${PREFIX_ROOT}/include/:${C_INCLUDE_PATH:=/usr/local/include/}
    export C_INCLUDE_PATH 
    CPLUS_INCLUDE_PATH=${PREFIX_ROOT}/include/:${CPLUS_INCLUDE_PATH:=/usr/local/include/}
    export CPLUS_INCLUDE_PATH
    LIBRARY_PATH=${PREFIX_ROOT}/lib:${LIBRARY_PATH:=/usr/local/lib/}
    export LIBRARY_PATH 
                
    make SHARED=yes && make install SHARED=yes
    
    if [ 0 -ne ${?} ]; then
        echo "build rtmpdump fail!\n"
        return 1
    fi
    
    return 0
}

build_opus()
{
    module_pack="opus-1.2.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the opus package from server\n"
        wget https://archive.mozilla.org/pub/opus/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd opus*
    ./configure --prefix=${PREFIX_ROOT} 
    if [ 0 -ne ${?} ]; then
        echo "configure opus fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build opus fail!\n"
        return 1
    fi
    
    return 0
}
build_libunistring()
{
    module_pack="libunistring-0.9.8.tar.xz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libunistring package from server\n"
        wget http://ftp.gnu.org/gnu/libunistring/${module_pack}
    fi
    tar -Jxvf ${module_pack}
    
    cd libunistring*
    ./configure --prefix=${PREFIX_ROOT} --libdir=${PREFIX_ROOT}/lib/ --includedir=${PREFIX_ROOT}/include/ 
    if [ 0 -ne ${?} ]; then
        echo "configure libunistring fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libunistring fail!\n"
        return 1
    fi
    
    return 0
}
build_nettle()
{
    module_pack="nettle-3.4.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the nettle package from server\n"
        wget https://ftp.gnu.org/gnu/nettle/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd nettle*
    export PKG_CONFIG_PATH=${PREFIX_ROOT}/lib/pkgconfig/
    export CFLAGS="-I${PREFIX_ROOT}/include"
    export LDFLAGS="-L${PREFIX_ROOT}/lib -ldl"
    ./configure --prefix=${PREFIX_ROOT} --libdir=${PREFIX_ROOT}/lib/ --includedir=${PREFIX_ROOT}/include/
    if [ 0 -ne ${?} ]; then
        echo "configure nettle fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build nettle fail!\n"
        return 1
    fi
    
    return 0
}

build_gmp()
{
    module_pack="gmp-6.1.2.tar.bz2"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the gmp package from server\n"
        wget https://gmplib.org/download/gmp/${module_pack}
    fi
    tar -jxvf ${module_pack}
    
    cd gmp*
    ./configure --prefix=${PREFIX_ROOT} --libdir=${PREFIX_ROOT}/lib/ --includedir=${PREFIX_ROOT}/include/ 
    if [ 0 -ne ${?} ]; then
        echo "configure gmp fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build gmp fail!\n"
        return 1
    fi
    
    return 0
}
build_gnutls()
{
    module_pack="gnutls-3.5.17.tar.xz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the gnutls package from server\n"
        wget https://www.gnupg.org/ftp/gcrypt/gnutls/v3.5/${module_pack}
    fi
    tar -Jxvf ${module_pack}
    
    cd gnutls*
    export NETTLE_CFLAGS="-I${PREFIX_ROOT}/include" NETTLE_LIBS="-L${PREFIX_ROOT}/lib/ -lnettle"
    export HOGWEED_CFLAGS="-I${PREFIX_ROOT}/include" HOGWEED_LIBS="-L${PREFIX_ROOT}/lib/ -lhogweed"

    ./configure --prefix=${PREFIX_ROOT} --libdir=${PREFIX_ROOT}/lib/ --includedir=${PREFIX_ROOT}/include/ --with-included-unistring --with-libunistring-prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure gnutls fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build gnutls fail!\n"
        return 1
    fi
    
    return 0
}
build_soxr()
{
    module_pack="soxr-code.zip"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the soxr package from server\n"
        wget https://sourceforge.net/code-snapshots/git/s/so/soxr/code.git/soxr-code-e064aba6ac16fb8f1fa64bc692e54230cec1b3af.zip -O ${module_pack}
    fi
    unzip -o ${module_pack}
    
    cd soxr*
    mkdir build
    cd build
    cmake -Wno-dev -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${PREFIX_ROOT} ..
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build soxr fail!\n"
        return 1
    fi
    
    return 0
}
build_speex()
{
    module_pack="speex-1.2.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the speex package from server\n"
        wget http://downloads.us.xiph.org/releases/speex/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd speex*
    ./configure --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure speex fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build speex fail!\n"
        return 1
    fi
    
    return 0
}
build_ogg()
{
    module_pack="libogg-1.3.3.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the ogg package from server\n"
        wget http://downloads.xiph.org/releases/ogg/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libogg*
    ./configure --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure ogg fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build ogg fail!\n"
        return 1
    fi
    
    return 0
}
build_vorbis()
{
    module_pack="libvorbis-1.3.5.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the vorbis package from server\n"
        wget http://downloads.xiph.org/releases/vorbis/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libvorbis*
    ./configure --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure vorbis fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build vorbis fail!\n"
        return 1
    fi
    
    return 0
}
build_theora()
{
    module_pack="libtheora-1.1.1.tar.bz2"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the theora package from server\n"
        wget http://downloads.xiph.org/releases/theora/${module_pack}
    fi
    tar -jxvf ${module_pack}
    
    cd libtheora*
    ./configure --prefix=${PREFIX_ROOT} --with-ogg=${PREFIX_ROOT} --with-vorbis=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure theora fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build theora fail!\n"
        return 1
    fi
    
    return 0
}
build_vidstab()
{
    module_pack="vidstab-v1.1.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the vidstab package from server\n"
        wget https://github.com/georgmartius/vid.stab/archive/v1.1.0.tar.gz -O ${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd vid.stab*
    cmake -DCMAKE_INSTALL_PREFIX:PATH=${PREFIX_ROOT} -DBUILD_SHARED_LIBS=NO
    if [ 0 -ne ${?} ]; then
        echo "configure vidstab fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build vidstab fail!\n"
        return 1
    fi
    
    return 0
}

build_yasm()
{
    module_pack="yasm-1.3.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the yasm package from server\n"
        wget http://www.tortall.net/projects/yasm/releases/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd yasm*
    ./configure --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure yasm fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build yasm fail!\n"
        return 1
    fi
    
    return 0
}
build_libvpx()
{
    module_pack="libvpx-v1.6.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libvpx package from server\n"
        wget https://github.com/webmproject/libvpx/archive/v1.6.1.tar.gz -O ${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libvpx*
    export PATH=${PREFIX_ROOT}/bin:${PATH}
    ./configure --prefix=${PREFIX_ROOT} --enable-pic --enable-shared
    if [ 0 -ne ${?} ]; then
        echo "configure libvpx fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libvpx fail!\n"
        return 1
    fi
    
    return 0
}
build_libwebp()
{
    module_pack="libwebp-0.6.1.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libwebp package from server\n"
        wget http://downloads.webmproject.org/releases/webp/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libwebp*
    ./configure --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure libwebp fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libwebp fail!\n"
        return 1
    fi
    
    return 0
}

build_xvidcore()
{
    module_pack="xvidcore-1.3.5.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the xvidcore package from server\n"
        wget https://downloads.xvid.com/downloads/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd xvidcore*/build/generic
    
    ./configure --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure xvidcore fail!\n"
        return 1
    fi

    make
    
    if [ 0 -ne ${?} ]; then
        echo "build xvidcore fail!\n"
        return 1
    fi
    
    make install
    
    return 0
}
build_zimg()
{
    module_pack="zimg-v3.1.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the zimg package from server\n"
        wget https://github.com/buaazp/zimg/archive/v3.1.0.tar.gz -O ${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd zimg*
    ./configure --prefix=${PREFIX_ROOT}
    if [ 0 -ne ${?} ]; then
        echo "configure zimg fail!\n"
        return 1
    fi
    
    make&&make install
    
    if [ 0 -ne ${?} ]; then
        echo "build zimg fail!\n"
        return 1
    fi
    
    return 0
}
build_libass()
{
    module_pack="libass-0.14.0.tar.gz"
    cd ${THIRD_ROOT}
    if [ ! -f ${THIRD_ROOT}${module_pack} ]; then
        echo "start get the libass package from server\n"
        wget https://github.com/libass/libass/releases/download/0.14.0/${module_pack}
    fi
    tar -zxvf ${module_pack}
    
    cd libass*
    ./configure --prefix=${PREFIX_ROOT} 
    if [ 0 -ne ${?} ]; then
        echo "configure libass fail!\n"
        return 1
    fi
                
    make && make install
    
    if [ 0 -ne ${?} ]; then
        echo "build libass fail!\n"
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
    build_libxml2
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_freetype
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    
    build_frei0r_plugins
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_fdkaac
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_x264
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_x265
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_ffad
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_faac
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_lame
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_opencore_amr
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_openjpeg
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_opus
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_xzutils
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libunistring
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_nettle
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_gmp
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_gnutls
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_openssl
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_rtmpdump
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_soxr
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_speex
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_ogg
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_vorbis
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_theora
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_vidstab
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_yasm
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libvpx
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_libwebp
    if [ 0 -ne ${?} ]; then
        return 1
    fi
    build_xvidcore
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
    echo "-t: set the 3rd party code path"
    echo " e.g.: $0 -p /usr/local/" 
}

if [ $# == 0 ]; then
    echo "build and install to ${PREFIX_ROOT}"
    setup
    exit
fi

while getopts "hp:t:" opt; do  
  case $opt in  
    h)  
      show_help   
      ;;  
    p)  
      export PREFIX_ROOT=${OPTARG}        
      ;; 
    t)  
      export THIRD_ROOT=${OPTARG}        
      ;;       
    ?)  
      echo "Invalid option: ${OPTARG}"   
      ;;  
  esac  
done
 
setup
exit 
