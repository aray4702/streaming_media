# The fuzz targets this build has and how each is built (#989, C10), shared by fuzz/CMakeLists.txt and the
# benchmarks' fuzz-target throughput (bench/fuzz_bench.cc, 10.4): SM_FUZZ_TARGETS, and per target <t> the library
# it links (SM_FUZZ_DEPS_<t>, sm::rtp when unset), its source (SM_FUZZ_SOURCE_<t>, <t>_fuzzer.cc when unset), extra
# sources and include directories (SM_FUZZ_SOURCES_<t>, SM_FUZZ_INCLUDES_<t>) and its corpus
# (SM_FUZZ_CORPUS_<t>, corpus/<t>/ when unset). SM_FUZZ_SOURCE_<t> is relative to fuzz/; the extra sources and
# include directories are absolute paths.
set(SM_FUZZ_TARGETS
  h264_depacketizer
  packet_buffer
  red
  rtcp
  rtp_packet
  stun
  tcp_framing
  transport_feedback
  turn
  ulpfec
)
# dcsctp's packet layer (#1032) and socket (#1060), only when data channels are compiled in.
if(SM_WITH_DATACHANNEL)
  list(APPEND SM_FUZZ_TARGETS dcsctp_packet dcsctp_socket)
  set(SM_FUZZ_DEPS_dcsctp_packet sm::dcsctp)
  # The socket (#1060): net/dcsctp/fuzzers/dcsctp_fuzzers.cc's FuzzSocket, ported as dcsctp_fuzzers.cc.
  set(SM_FUZZ_DEPS_dcsctp_socket sm::dcsctp)
  set(SM_FUZZ_SOURCES_dcsctp_socket ${PROJECT_SOURCE_DIR}/fuzz/dcsctp_fuzzers.cc)
endif()
# DTLS (#1058), only with sm::tls: dtls_record on sm::tls's backend, and on the
# other one too where both are built (SM_TLS_CROSS_TEST), and dtls_transport
# (p2p::DtlsTransport over the fake ICE transport). All three replay
# corpus/dtls_record/.
if(SM_WITH_TLS)
  list(APPEND SM_FUZZ_TARGETS dtls_record dtls_transport)
  set(SM_FUZZ_DEPS_dtls_record sm::tls)
  set(SM_FUZZ_DEPS_dtls_transport sm::p2p_dtls)
  set(SM_FUZZ_INCLUDES_dtls_transport ${PROJECT_SOURCE_DIR}/tests/unit/p2p_dtls)
  set(SM_FUZZ_CORPUS_dtls_transport dtls_record)
  if(SM_BUILD_TESTS AND SM_TLS_CROSS_TEST AND SM_TLS_OTHER_BACKEND)
    set(t dtls_record_${SM_TLS_OTHER_BACKEND})
    list(APPEND SM_FUZZ_TARGETS ${t})
    set(SM_FUZZ_SOURCE_${t} dtls_record_fuzzer.cc)
    set(SM_FUZZ_DEPS_${t} sm::tls_${SM_TLS_OTHER_BACKEND})
    set(SM_FUZZ_CORPUS_${t} dtls_record)
  endif()
endif()
# SDP (#1062), only with the connectivity TLS library (sm::pc_sdp is on sm::tls).
if(SM_WITH_TLS)
  list(APPEND SM_FUZZ_TARGETS sdp_parser)
  set(SM_FUZZ_DEPS_sdp_parser sm::pc_sdp)
endif()
# The library each target links: sm::rtp (which brings the RTP, RTCP, loss
# and timing parsers) unless set here.
# Extra sources: the STUN tests' HMAC-SHA1 and MD5 for the crypto hooks (the
# TURN and TCP framing targets use them for TURN's credentials).
foreach(t stun turn tcp_framing)
  set(SM_FUZZ_DEPS_${t} sm::p2p)
  set(SM_FUZZ_SOURCES_${t} ${PROJECT_SOURCE_DIR}/tests/unit/p2p/test_crypto.cc)
  set(SM_FUZZ_INCLUDES_${t} ${PROJECT_SOURCE_DIR}/tests/unit/p2p)
endforeach()
