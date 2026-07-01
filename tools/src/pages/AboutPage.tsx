import React from 'react';

const supportItems = [
  '72 项驱动器参数配置',
  '加密 bin 文件生成',
  '加密 bin 文件解析复用',
  'AES-256-GCM Payload 加密',
];

const infoSections = [
  {
    icon: 'grid',
    title: '支持功能',
    body: (
      <ul className="about-feature-list">
        {supportItems.map((item) => (
          <li key={item}>{item}</li>
        ))}
      </ul>
    ),
  },
  {
    icon: 'pen',
    title: '软件信息',
    body: (
      <div className="about-field-list">
        <div>
          <span>软件版本：</span>
          <strong>V1.0</strong>
        </div>
        <div>
          <span>发布时间：</span>
          <strong>{__APP_BUILD_TIME__}</strong>
        </div>
      </div>
    ),
  },
  {
    icon: 'target',
    title: '版权声明',
    body: (
      <p className="about-copyright">
        ©2026 北京联研国芯技术有限责任公司保留所有权利
      </p>
    ),
  },
  {
    icon: 'mail',
    title: '技术支持',
    body: <p className="about-support-mail">xieyaojie@uniedge.cn</p>,
  },
];

/**
 * "关于" page. Sibling of BuilderPage / ParserPage — selected via the
 * 关于 button in the side nav (same `view` state, same `.active` pattern).
 * No modal, no close button: leave by clicking another nav button.
 */
export const AboutPage: React.FC = () => {
  return (
    <section className="panel page-panel about-page">
      <div className="about-page-content device-about">
        <div className="device-about-screen" aria-label="关于信息">
          {infoSections.map((section) => (
            <section className="device-about-section" key={section.title}>
              <h3>
                <span className={`about-section-icon icon-${section.icon}`} aria-hidden="true" />
                {section.title}
              </h3>
              {section.body}
            </section>
          ))}
        </div>
      </div>
    </section>
  );
};
