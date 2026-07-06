set(MEDUSA_ROOT "${GAMECQ_MEDUSA_ROOT}")

if(NOT EXISTS "${MEDUSA_ROOT}/GCQ/MetaClient.cpp")
    message(FATAL_ERROR "GAMECQ_MEDUSA_ROOT does not point to a Medusa source tree: ${MEDUSA_ROOT}")
endif()

set(LIBNET_SOURCES
    # Medusa debug support used by Network, GCQ, Factory, and System.
    "${MEDUSA_ROOT}/Debug/Assert.cpp"
    "${MEDUSA_ROOT}/Debug/Error.cpp"
    "${MEDUSA_ROOT}/Debug/ExceptionHandler.cpp"
    "${MEDUSA_ROOT}/Debug/Log.cpp"
    "${MEDUSA_ROOT}/Debug/Profile.cpp"
    "${MEDUSA_ROOT}/Debug/SafeThread.cpp"

    # Factory/Widget support required by GCQ/MetaClient.h through WidgetWrap.
    "${MEDUSA_ROOT}/Factory/Broker.cpp"
    "${MEDUSA_ROOT}/Factory/BrokerArchive.cpp"
    "${MEDUSA_ROOT}/Factory/BrokerCache.cpp"
    "${MEDUSA_ROOT}/Factory/BrokerFolder.cpp"
    "${MEDUSA_ROOT}/Factory/ClassKey.cpp"
    "${MEDUSA_ROOT}/Factory/Factory.cpp"
    "${MEDUSA_ROOT}/Factory/FactoryTypes.cpp"
    "${MEDUSA_ROOT}/Factory/Widget.cpp"
    "${MEDUSA_ROOT}/Factory/WidgetWrap.cpp"

    # File and stream primitives.
    "${MEDUSA_ROOT}/File/Archive.cpp"
    "${MEDUSA_ROOT}/File/Dictionary.cpp"
    "${MEDUSA_ROOT}/File/File.cpp"
    "${MEDUSA_ROOT}/File/FileArchive.cpp"
    "${MEDUSA_ROOT}/File/FileChange.cpp"
    "${MEDUSA_ROOT}/File/FileDisk.cpp"
    "${MEDUSA_ROOT}/File/FileFIFO.cpp"
    "${MEDUSA_ROOT}/File/FileMap.cpp"
    "${MEDUSA_ROOT}/File/FileMemory.cpp"
    "${MEDUSA_ROOT}/File/FileMemoryBlocked.cpp"
    "${MEDUSA_ROOT}/File/FindFile.cpp"
    "${MEDUSA_ROOT}/File/Path.cpp"
    "${MEDUSA_ROOT}/File/Stream.cpp"

    # Runtime type metadata used by Widget/Factory serialization.
    "${MEDUSA_ROOT}/Reflection/Property.cpp"
    "${MEDUSA_ROOT}/Reflection/PropertyClass.cpp"
    "${MEDUSA_ROOT}/Reflection/PropertyList.cpp"
    "${MEDUSA_ROOT}/Reflection/Type.cpp"
    "${MEDUSA_ROOT}/Reflection/TypeCopy.cpp"
    "${MEDUSA_ROOT}/Reflection/Value.cpp"

    # Resource link base classes are referenced by Widget/Factory templates.
    "${MEDUSA_ROOT}/Resource/Collection.cpp"
    "${MEDUSA_ROOT}/Resource/Resource.cpp"
    "${MEDUSA_ROOT}/Resource/ResourceLink.cpp"
    "${MEDUSA_ROOT}/Resource/Text.cpp"

    # Core containers, strings, threading, compression, and utilities.
    "${MEDUSA_ROOT}/Standard/Atomic.cpp"
    "${MEDUSA_ROOT}/Standard/BitIterator.cpp"
    "${MEDUSA_ROOT}/Standard/Buffer.cpp"
    "${MEDUSA_ROOT}/Standard/Codec.cpp"
    "${MEDUSA_ROOT}/Standard/ColorFormat.cpp"
    "${MEDUSA_ROOT}/Standard/Constant.cpp"
    "${MEDUSA_ROOT}/Standard/CriticalSection.cpp"
    "${MEDUSA_ROOT}/Standard/Event.cpp"
    "${MEDUSA_ROOT}/Standard/Exception.cpp"
    "${MEDUSA_ROOT}/Standard/Expression.cpp"
    "${MEDUSA_ROOT}/Standard/FIFOBuffer.cpp"
    "${MEDUSA_ROOT}/Standard/Library.cpp"
    "${MEDUSA_ROOT}/Standard/LZW.cpp"
    "${MEDUSA_ROOT}/Standard/MD5.cpp"
    "${MEDUSA_ROOT}/Standard/Mutex.cpp"
    "${MEDUSA_ROOT}/Standard/Process.cpp"
    "${MEDUSA_ROOT}/Standard/Progress.cpp"
    "${MEDUSA_ROOT}/Standard/RandomNumber.cpp"
    "${MEDUSA_ROOT}/Standard/Referenced.cpp"
    "${MEDUSA_ROOT}/Standard/RegExpM.cpp"
    "${MEDUSA_ROOT}/Standard/Registry.cpp"
    "${MEDUSA_ROOT}/Standard/Settings.cpp"
    "${MEDUSA_ROOT}/Standard/String.cpp"
    "${MEDUSA_ROOT}/Standard/StringBuffer.cpp"
    "${MEDUSA_ROOT}/Standard/StringHash.cpp"
    "${MEDUSA_ROOT}/Standard/Thread.cpp"
    "${MEDUSA_ROOT}/Standard/ThreadPool.cpp"
    "${MEDUSA_ROOT}/Standard/Time.cpp"
    "${MEDUSA_ROOT}/Standard/Timer.cpp"
    "${MEDUSA_ROOT}/Standard/TimerWheel.cpp"
    "${MEDUSA_ROOT}/Standard/UniqueNumber.cpp"
    "${MEDUSA_ROOT}/Standard/UUE.cpp"
    "${MEDUSA_ROOT}/Standard/ZLIB.cpp"

    # Locale and command helpers used by MetaClient.
    "${MEDUSA_ROOT}/System/CommandTarget.cpp"
    "${MEDUSA_ROOT}/System/Keyboard.cpp"
    "${MEDUSA_ROOT}/System/Locale.cpp"
    "${MEDUSA_ROOT}/System/LocalizedString.cpp"

    # Client-side transport and mirror/process helpers.
    "${MEDUSA_ROOT}/Network/Client.cpp"
    "${MEDUSA_ROOT}/Network/FileSocket.cpp"
    "${MEDUSA_ROOT}/Network/LogClient.cpp"
    "${MEDUSA_ROOT}/Network/MirrorClient.cpp"
    "${MEDUSA_ROOT}/Network/Network.cpp"
    "${MEDUSA_ROOT}/Network/ProfilerClient.cpp"
    "${MEDUSA_ROOT}/Network/Socket.cpp"
    "${MEDUSA_ROOT}/Network/SocketList.cpp"

    # Lobby protocol clients.
    "${MEDUSA_ROOT}/GCQ/MasterClient.cpp"
    "${MEDUSA_ROOT}/GCQ/MetaClient.cpp"
    "${MEDUSA_ROOT}/GCQ/MetaClientCommand.cpp"
    "${MEDUSA_ROOT}/GCQ/MetaClientMID.cpp"
    "${MEDUSA_ROOT}/GCQ/ProcessClient.cpp"

    # Third-party sources required by Standard/RegExpM.cpp and Standard/ZLIB.cpp.
    "${MEDUSA_ROOT}/ThirdParty/re2/frontend.c"
    "${MEDUSA_ROOT}/ThirdParty/re2/memory.c"
    "${MEDUSA_ROOT}/ThirdParty/re2/regerror.c"
    "${MEDUSA_ROOT}/ThirdParty/re2/regexp.c"
    "${MEDUSA_ROOT}/ThirdParty/re2/regsub.c"
    "${MEDUSA_ROOT}/ThirdParty/re2/report.c"
    "${MEDUSA_ROOT}/ThirdParty/re2/widechar.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/adler32.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/compress.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/crc32.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/deflate.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/inffast.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/inflate.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/inftrees.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/trees.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/uncompr.c"
    "${MEDUSA_ROOT}/ThirdParty/zlib/zutil.c"
)

add_library(libnet STATIC ${LIBNET_SOURCES})
add_library(GameCQ::libnet ALIAS libnet)

target_include_directories(libnet
    PUBLIC
        "${MEDUSA_ROOT}"
        "${MEDUSA_ROOT}/ThirdParty"
)

target_compile_definitions(libnet
    PUBLIC
        WIN32
        _WINDOWS
        MEDUSA_DLL
        REGEXP_UNICODE
        NETWORK_DLL
        GCQ_DLL
        Z_SOLO
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
)

if(MSVC)
    target_compile_options(libnet PRIVATE /W3 /wd4251 /wd4275 /wd4996)
endif()

if(WIN32)
    target_link_libraries(libnet
        PUBLIC
            ws2_32
            advapi32
            user32
            ole32
            oleaut32
            wbemuuid
    )
endif()

source_group(TREE "${MEDUSA_ROOT}" FILES ${LIBNET_SOURCES})
