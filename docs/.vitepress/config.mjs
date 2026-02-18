import { defineConfig } from 'vitepress'

export default defineConfig({
  title: 'UVRPC',
  description: 'Ultra-Fast RPC Framework - Zero threads, Zero locks, Zero global variables',
  
  lang: 'en-US',
  
  base: '/uvrpc/',
  
  ignoreDeadLinks: true,
  
  themeConfig: {
    nav: [
      { text: 'Guide', link: '/en/guide/' },
      { text: 'API', link: '/en/api/' },
      { text: 'Examples', link: '/en/examples/' },
      { text: '中文文档', link: '/zh/' }
    ],

    sidebar: {
      '/en/': [
        {
          text: 'Getting Started',
          items: [
            { text: 'Introduction', link: '/en/' },
            { text: 'Quick Start', link: '/en/quick-start' },
            { text: 'Build & Install', link: '/en/build-install' }
          ]
        },
        {
          text: 'Guide',
          items: [
            { text: 'API Guide', link: '/en/guide/api-guide' },
            { text: 'Design Philosophy', link: '/en/guide/design-philosophy' },
            { text: 'Single Thread Model', link: '/en/guide/single-thread-model' }
          ]
        },
        {
          text: 'API Reference',
          items: [
            { text: 'API Reference', link: '/en/api/' },
            { text: 'Generated API', link: '/en/api/generated-api' }
          ]
        },
        {
          text: 'Architecture',
          items: [
            { text: 'Architecture', link: '/en/architecture/' },
            { text: 'Integration', link: '/en/architecture/integration' }
          ]
        },
        {
          text: 'Development',
          items: [
            { text: 'Coding Standards', link: '/en/development/coding-standards' },
            { text: 'Doxygen Examples', link: '/en/development/doxygen-examples' },
            { text: 'Migration Guide', link: '/en/development/migration' }
          ]
        }
      ],

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
            { text: 'API 指南', link: '/zh/guide/api-guide' },
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
    },

    socialLinks: [
      { icon: 'github', link: 'https://github.com/adam-ikari/uvrpc' }
    ],

    search: {
      provider: 'local'
    }
  }
})