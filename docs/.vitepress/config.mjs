import { defineConfig } from 'vitepress'

const SITE = 'https://adam-ikari.github.io/uvrpc'

export default defineConfig({
  title: 'UVRPC',
  titleTemplate: ':title · UVRPC',
  description: 'Ultra-fast C99 RPC framework built on libuv + FlatBuffers. Zero threads, zero locks, zero globals. ~245k req/s in-process.',

  lang: 'en-US',
  base: '/uvrpc/',

  head: [
    ['meta', { name: 'keywords', content: 'RPC, C99, libuv, FlatBuffers, high-performance RPC, zero-copy, async RPC, in-process RPC, IPC, TCP, UDP, single-threaded RPC' }],
    ['meta', { name: 'author', content: 'UVRPC Team' }],
    ['meta', { property: 'og:type', content: 'website' }],
    ['meta', { property: 'og:site_name', content: 'UVRPC' }],
    ['meta', { property: 'og:title', content: 'UVRPC — Ultra-Fast C99 RPC Framework' }],
    ['meta', { property: 'og:description', content: 'Zero threads, zero locks, zero globals. ~245k req/s in-process. TCP/UDP/IPC/INPROC/SAMELOOP.' }],
    ['meta', { property: 'og:url', content: SITE }],
    ['meta', { name: 'twitter:card', content: 'summary_large_image' }],
    ['meta', { name: 'twitter:title', content: 'UVRPC — Ultra-Fast C99 RPC Framework' }],
    ['meta', { name: 'twitter:description', content: 'Zero threads, zero locks, zero globals. ~245k req/s in-process. Built on libuv + FlatBuffers.' }],
    ['link', { rel: 'canonical', href: SITE + '/' }],
    ['meta', { name: 'theme-color', content: '#3aa675' }]
  ],

  // Sitemap (VitePress 1.x built-in) — submitted to search engines. Hostname
  // includes the base path so URLs resolve correctly under /uvrpc/.
  sitemap: {
    hostname: SITE + '/'
  },

  // Dead links fail the build (good for SEO/correctness). Previously
  // ignoreDeadLinks: true masked ~20 broken links; all have been fixed.
  ignoreDeadLinks: false,

  // Exclude non-site content from the build/sitemap.
  srcExclude: ['**/superpowers/**', '**/PRIMITIVES_GUIDE.md'],

  locales: {
    root: {
      label: 'English',
      lang: 'en-US',
      link: '/',
      themeConfig: {
        nav: [
          { text: 'Guide', link: '/guide/' },
          { text: 'API', link: '/api/' }
        ],
        sidebar: {
          '/': [
            {
              text: 'Getting Started',
              items: [
                { text: 'Introduction', link: '/' },
                { text: 'Quick Start', link: '/quick-start' },
                { text: 'Build & Install', link: '/build-install' }
              ]
            },
            {
              text: 'Guide',
              items: [
                { text: 'API Guide', link: '/guide/api-guide' },
                { text: 'Benchmark', link: '/guide/benchmark' },
                { text: 'Design Philosophy', link: '/guide/design-philosophy' },
                { text: 'Single Thread Model', link: '/guide/single-thread-model' }
              ]
            },
            {
              text: 'API Reference',
              items: [
                { text: 'API Reference', link: '/api/' },
                { text: 'Generated API', link: '/api/generated-api' }
              ]
            },
            {
              text: 'Architecture',
              items: [
                { text: 'Architecture', link: '/architecture/' },
                { text: 'Integration', link: '/architecture/integration' }
              ]
            },
            {
              text: 'Development',
              items: [
                { text: 'Coding Standards', link: '/development/coding-standards' },
                { text: 'Doxygen Examples', link: '/development/doxygen-examples' },
                { text: 'Migration Guide', link: '/development/migration' }
              ]
            }
          ]
        }
      }
    },
    zh: {
      label: '简体中文',
      lang: 'zh-CN',
      link: '/zh/',
      description: '超快速 C99 RPC 框架，基于 libuv + FlatBuffers。零线程、零锁、零全局变量。进程内约 245,000 req/s。',
      head: [
        ['meta', { property: 'og:title', content: 'UVRPC — 超快速 C99 RPC 框架' }],
        ['meta', { property: 'og:description', content: '零线程、零锁、零全局变量。进程内约 245,000 req/s。TCP/UDP/IPC/INPROC/SAMELOOP。' }],
        ['meta', { name: 'twitter:title', content: 'UVRPC — 超快速 C99 RPC 框架' }],
        ['meta', { name: 'twitter:description', content: '零线程、零锁、零全局变量。进程内约 245,000 req/s。基于 libuv + FlatBuffers。' }]
      ],
      themeConfig: {
        nav: [
          { text: '指南', link: '/zh/guide/' },
          { text: 'API', link: '/api/' }
        ],
        sidebar: {
          '/zh/': [
            {
              text: '开始使用',
              items: [
                { text: '介绍', link: '/zh/' },
                { text: '快速开始', link: '/zh/guide/quick-start' },
                { text: '构建安装', link: '/zh/build-install' }
              ]
            },
            {
              text: '指南',
              items: [
                { text: 'API 指南', link: '/zh/guide/api-guide' },
                { text: '性能测试', link: '/zh/guide/benchmark' },
                { text: '设计哲学', link: '/zh/guide/design-philosophy' },
                { text: '单线程模型', link: '/zh/guide/single-thread-model' }
              ]
            },
            {
              text: '开发',
              items: [
                { text: '编码规范', link: '/zh/development/coding-standards' },
                { text: 'Doxygen 示例', link: '/zh/development/doxygen-examples' }
              ]
            }
          ]
        }
      }
    }
  },
  
  themeConfig: {
    socialLinks: [
      { icon: 'github', link: 'https://github.com/adam-ikari/uvrpc' }
    ],

    search: {
      provider: 'local'
    },

    // Language switching dropdown
    outline: {
      label: 'On this page',
      level: [2, 3]
    }
  }
})