import React from 'react';

/**
 * "关于" page. Sibling of BuilderPage / ParserPage — selected via the
 * 关于 button in the side nav (same `view` state, same `.active` pattern).
 * No modal, no close button: leave by clicking another nav button.
 */
export const AboutPage: React.FC = () => {
  return (
    <section className="panel page-panel about-page">
      <div className="about-page-content">
        <section>
          <h3>软件用途</h3>
          <p>
            本工具用于编辑 72 个固定参数，并生成 ESP32 端可解析的加密 bin 文件。
            参数地址固定为 0~71，每个参数包含名称、默认值、类型和权限。
          </p>
        </section>

        <section>
          <h3>参数构建</h3>
          <p>
            在参数构建页面中，可以编辑 72 个固定参数，保存工程文件，
            或生成加密 bin 文件。生成前会自动校验参数数量、地址范围、
            名称长度和默认值范围。
          </p>
        </section>

        <section>
          <h3>参数解析</h3>
          <p>
            在参数解析页面中，可以加载由本工具生成的加密 bin 文件，
            解密并显示完整的 72 个参数，用于工程侧验证文件内容。
          </p>
        </section>

        <section>
          <h3>加密说明</h3>
          <p>
            发布用 bin 文件采用 AES-256-GCM 加密。中文名称、默认值、
            类型和权限均位于加密 Payload 内，第三方工具无法直接明文解析。
            17 字节文件头作为 AAD 参与认证，文件被篡改后会解析失败。
          </p>
        </section>

        <section>
          <h3>文件格式</h3>
          <p>
            完整文件由 17 字节简化 Header、加密 Payload 以及 16 字节 GCM Tag 组成。
            Header 仅包含 Magic、格式版本和随机 Nonce，不再包含 Product ID、Key ID
            或参数业务字段。
          </p>
        </section>

        <section>
          <h3>使用流程</h3>
          <ol>
            <li>在左侧选择「参数构建」页面。</li>
            <li>编辑 72 个参数（地址 0~35 在左栏，36~71 在右栏）。</li>
            <li>点击「生成加密 bin」导出文件（导出前会自动校验参数，不通过会中止导出）。</li>
            <li>需要验证时，切换到「参数解析」页面加载 bin 文件。</li>
          </ol>
        </section>

        <section>
          <h3>注意事项</h3>
          <ul>
            <li>参数地址固定为 0~71，不可增删。</li>
            <li>参数名称最多 20 个字符。</li>
            <li>默认值范围为 0~65535。</li>
            <li>隐藏参数在 ESP32 客户界面中应不可见。</li>
            <li>工程文件是内部明文文件，发布给 ESP32 的文件应使用加密 bin。</li>
          </ul>
        </section>
      </div>
    </section>
  );
};
