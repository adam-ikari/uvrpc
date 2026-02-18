---
layout: home

hero:
  name: "UVRPC"
  text: "Ultra-Fast RPC Framework"
  tagline: "Zero threads, Zero locks, Zero global variables"
  actions:
    - theme: brand
      text: Get Started
      link: /en/quick-start
    - theme: alt
      text: View on GitHub
      link: https://github.com/adam-ikari/uvrpc

features:
  - title: 🚀 Ultra-Fast
    details: "Based on libuv event loop and FlatBuffers serialization, achieving 125,000+ ops/s for INPROC transport."
  - title: 🎯 Minimal Design
    details: "Zero threads, zero locks, zero global variables. All I/O managed by libuv event loop."
  - title: 🔌 Multiple Transports
    details: "Support for TCP, UDP, IPC, and INPROC transports with unified API."
  - title: 📦 Zero-Copy
    details: "FlatBuffers binary serialization minimizes memory copying for maximum performance."
  - title: 🔄 Loop Injection
    details: "Support custom libuv loop, multi-instance independent or shared loop."
  - title: 📚 Type-Safe
    details: "FlatBuffers DSL generates type-safe APIs with compile-time checking."

---