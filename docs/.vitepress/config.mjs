import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'UVRPC',
  description: 'Ultra-Fast RPC Framework - Zero threads, Zero locks, Zero global variables',
  
  lang: 'en-US',
  
  base: '/uvrpc/',
  
  ignoreDeadLinks: true,
  
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
                { text: '快速开始', link: '/zh/quick-start' },
                { text: '构建安装', link: '/zh/build-install' }
              ]
            },
            {
              text: '指南',
              items: [
                { text: '快速开始', link: '/zh/guide/quick-start' },
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