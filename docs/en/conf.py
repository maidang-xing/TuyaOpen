# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'TuyaOpen Development Guide'
copyright = '2021-%Y, Tuya Inc'
author = 'Tuya'
release = '1.2.0'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.viewcode',
    'sphinx.ext.todo',
    'myst_parser',
    'sphinx_markdown_tables',
    'sphinx_copybutton',
    'sphinxcontrib.mermaid',
    'sphinx_design',
    'sphinx_sitemap',
]

templates_path = ['_templates']
exclude_patterns = []
source_suffix = ['.rst', '.md']

language = 'en'

# 语法高亮设置
pygments_style = 'friendly'

html_logo = '../images/TuyaOpen.png'

def setup(app):
    app.add_css_file('css/custom.css')

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output

html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']

html_theme_options = {
    'display_version': True,
    'prev_next_buttons_location': 'both',
    'style_external_links': False,
    'sticky_navigation': True,
    'navigation_depth': 4,
    'includehidden': True,
    'titles_only': False,
    'collapse_navigation': False,
    'logo_only': True,
    'body_max_width': None,  # 取消页面宽度限制
    'sidebarwidth': '25%',  # 调整侧边栏宽度
}

html_js_files = [
    'js/custom.js',  # 如果有自定义 JavaScript 文件
    'js/include_html.js'  # 如果需要引入其他 JavaScript 文件
]

# -- GitHub 相关配置 ---------------------------------------------------
html_context = {
    # GitHub 仓库设置（必需）
    "display_github": True,  # 启用 GitHub 链接
    "github_user": "tuya",   # 组织/用户名
    "github_repo": "TuyaOpen",  # 仓库名
    "github_version": "master",  # 默认分支（如 main/master）
    
    # 页面路径配置（自动生成编辑链接）
    "conf_py_path": "/docs/en/",  # 配置文件的仓库相对路径
}

highlight_language = 'c'  # 默认高亮C语言代码
primary_domain = 'c'      # 主文档域为C语言