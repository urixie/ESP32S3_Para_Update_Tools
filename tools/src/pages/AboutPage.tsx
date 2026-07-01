import React, { useMemo, useState } from 'react';

type AboutSectionKey =
  | 'intro'
  | 'features'
  | 'workflow'
  | 'file'
  | 'version'
  | 'copyright'
  | 'support';

interface AboutNavItem {
  key: AboutSectionKey;
  title: string;
  desc: string;
}

interface InfoCard {
  title: string;
  text: string;
}

const aboutNavItems: AboutNavItem[] = [
  { key: 'intro', title: '软件简介', desc: '工具定位与适用场景' },
  { key: 'features', title: '功能说明', desc: '主要能力与使用范围' },
  { key: 'workflow', title: '操作流程', desc: '生成与解析步骤' },
  { key: 'file', title: '文件说明', desc: '参数文件使用注意事项' },
  { key: 'version', title: '版本信息', desc: '版本、构建与运行平台' },
  { key: 'copyright', title: '版权信息', desc: '授权与使用声明' },
  { key: 'support', title: '技术支持', desc: '问题反馈与支持范围' },
];

const featureCards: InfoCard[] = [
  { title: '参数配置', text: '支持 72 项驱动器参数的名称、默认值、类型和权限维护。' },
  { title: '工程管理', text: '支持新建、保存、加载工程文件，便于后续维护和版本管理。' },
  { title: '文件导出', text: '支持将当前参数配置导出为设备端可识别的专用 bin 文件。' },
  { title: '文件解析', text: '支持解析由本工具生成的参数 bin 文件，并查看参数内容。' },
  { title: '结果复用', text: '支持将解析结果复用到参数配置界面，继续修改后重新导出。' },
  { title: '参数校验', text: '导出前自动检查参数合法性，减少错误配置进入设备端。' },
];

const buildSteps = [
  '新建工程或加载已有工程文件。',
  '编辑参数名称、默认值、类型和权限。',
  '填写板卡名称，作为设备端识别信息。',
  '执行参数校验，确认所有参数合法。',
  '导出专用参数 bin 文件，并交由设备端使用。',
];

const parseSteps = [
  '选择已有参数 bin 文件。',
  '查看解析出的板卡名称与参数内容。',
  '根据需要复用到参数配置界面。',
  '继续修改参数，并重新导出新的参数文件。',
];

const supportScope = [
  '参数配置工具使用问题',
  '参数文件导出失败',
  '参数文件解析失败',
  '设备端参数识别异常',
];

const AboutPage: React.FC = () => {
  const [activeSection, setActiveSection] = useState<AboutSectionKey>('intro');

  const activeNav = useMemo(
    () => aboutNavItems.find((item) => item.key === activeSection) ?? aboutNavItems[0],
    [activeSection],
  );

  const renderContent = () => {
    switch (activeSection) {
      case 'intro':
        return (
          <div className="about-content-stack">
            <div className="about-hero-card">
              <div>
                <div className="about-kicker">UniEdge Parameter Tool</div>
                <h2>驱动器参数配置工具</h2>
                <p>
                  本工具用于 UniEdge 驱动器参数文件的构建、导出与解析。用户可通过图形化界面维护参数信息，
                  并生成可供设备端识别的参数 bin 文件。
                </p>
              </div>
              <div className="about-version-pill">V1.0.0</div>
            </div>

            <div className="about-card-grid two-columns">
              <article className="about-info-card">
                <span className="about-card-index">01</span>
                <h3>面向参数维护</h3>
                <p>适合用于驱动器参数表维护、工程文件管理、参数文件导出和现场参数复用。</p>
              </article>
              <article className="about-info-card">
                <span className="about-card-index">02</span>
                <h3>面向设备交付</h3>
                <p>导出的参数文件用于配套设备固件识别，避免用户手动维护复杂的参数数据结构。</p>
              </article>
            </div>
          </div>
        );

      case 'features':
        return (
          <div className="about-card-grid">
            {featureCards.map((card, index) => (
              <article className="about-info-card" key={card.title}>
                <span className="about-card-index">{String(index + 1).padStart(2, '0')}</span>
                <h3>{card.title}</h3>
                <p>{card.text}</p>
              </article>
            ))}
          </div>
        );

      case 'workflow':
        return (
          <div className="about-content-stack">
            <article className="about-flow-card">
              <div className="about-flow-title">参数文件生成流程</div>
              <ol className="about-step-list">
                {buildSteps.map((step) => (
                  <li key={step}>{step}</li>
                ))}
              </ol>
            </article>

            <article className="about-flow-card secondary">
              <div className="about-flow-title">参数文件解析流程</div>
              <ol className="about-step-list">
                {parseSteps.map((step) => (
                  <li key={step}>{step}</li>
                ))}
              </ol>
            </article>
          </div>
        );

      case 'file':
        return (
          <div className="about-content-stack">
            <article className="about-notice-card">
              <h3>文件使用说明</h3>
              <p>
                导出的 bin 文件为专用参数文件，仅适用于配套的软件工具和设备固件。请勿手动修改 bin 文件内容，
                文件被修改、损坏或来源不正确时，可能导致解析失败或设备无法识别。
              </p>
            </article>

            <div className="about-card-grid two-columns">
              <article className="about-info-card">
                <span className="about-card-index">建议</span>
                <h3>保存工程文件</h3>
                <p>建议同步保存工程文件，便于后续维护、参数追溯和重新导出。</p>
              </article>
              <article className="about-info-card">
                <span className="about-card-index">注意</span>
                <h3>避免手动编辑</h3>
                <p>不要使用十六进制编辑器或第三方工具直接修改 bin 文件。</p>
              </article>
            </div>
          </div>
        );

      case 'version':
        return (
          <div className="about-meta-grid">
            <div className="about-meta-item">
              <span>软件名称</span>
              <strong>UniEdge 驱动器参数配置工具</strong>
            </div>
            <div className="about-meta-item">
              <span>当前版本</span>
              <strong>V1.0.0</strong>
            </div>
            <div className="about-meta-item">
              <span>构建时间</span>
              <strong>{__APP_BUILD_TIME__}</strong>
            </div>
            <div className="about-meta-item">
              <span>运行平台</span>
              <strong>Windows 桌面端</strong>
            </div>
            <div className="about-meta-item wide">
              <span>技术架构</span>
              <strong>Tauri + Rust</strong>
            </div>
          </div>
        );

      case 'copyright':
        return (
          <div className="about-content-stack">
            <article className="about-notice-card">
              <h3>版权声明</h3>
              <p>© 2026 北京联研国芯技术有限责任公司。保留所有权利。</p>
              <p>
                本软件仅限授权用户使用，未经许可不得复制、传播、修改、反编译或用于其他商业用途。
              </p>
            </article>
          </div>
        );

      case 'support':
        return (
          <div className="about-content-stack">
            <article className="about-support-card">
              <div>
                <span className="about-support-label">技术支持邮箱</span>
                <strong>xieyaojie@uniedge.cn</strong>
              </div>
            </article>

            <article className="about-flow-card">
              <div className="about-flow-title">支持范围</div>
              <ul className="about-support-list">
                {supportScope.map((item) => (
                  <li key={item}>{item}</li>
                ))}
              </ul>
            </article>
          </div>
        );

      default:
        return null;
    }
  };

  return (
    <section className="panel page-panel about-panel">
      <div className="page-workspace about-workspace">
        <aside className="page-side-panel about-side-panel">
          <div className="side-panel-section">
            <div className="side-panel-title">关于工具</div>
            <nav className="about-subnav" aria-label="关于页面导航">
              {aboutNavItems.map((item) => (
                <button
                  key={item.key}
                  type="button"
                  className={activeSection === item.key ? 'active' : ''}
                  onClick={() => setActiveSection(item.key)}
                >
                  <span>{item.title}</span>
                  <small>{item.desc}</small>
                </button>
              ))}
            </nav>
          </div>

          <div className="about-side-summary">
            <div className="about-side-kicker">UniEdge Tools</div>
            <div className="about-side-name">参数配置工具</div>
            <div className="about-side-version">V1.0.0</div>
          </div>
        </aside>

        <div className="page-main-panel about-main-panel">
          <header className="about-main-header">
            <div>
              <div className="about-kicker">ABOUT</div>
              <h1>{activeNav.title}</h1>
              <p>{activeNav.desc}</p>
            </div>
          </header>

          <div className="about-main-content">{renderContent()}</div>
        </div>
      </div>
    </section>
  );
};

export { AboutPage };
