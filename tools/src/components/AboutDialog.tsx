import React, { useEffect } from 'react';

interface AboutDialogProps {
  onClose: () => void;
}

/**
 * Modal-style "About" dialog. Triggered by the top-right 关于 button.
 *
 * Closes on:
 *   - Clicking the × button in the header
 *   - Clicking the "我知道了" button in the footer
 *   - Clicking the dimmed overlay (backdrop)
 *   - Pressing Escape
 */
export const AboutDialog: React.FC<AboutDialogProps> = ({ onClose }) => {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [onClose]);

  return (
    <div
      className="about-overlay"
      onClick={onClose}
      role="dialog"
      aria-modal="true"
      aria-label="关于 Param Bin Tool"
    >
      <div
        className="about-dialog"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="about-header">
          <div>
            <h2>关于 Param Bin Tool</h2>
            <p>ESP32 参数加密 bin 上位机 · AES-256-GCM</p>
          </div>
          <button
            className="about-close"
            onClick={onClose}
            aria-label="关闭"
          >
            ×
          </button>
        </div>

        <div className="about-content">
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
              Header 作为 AAD 参与认证，文件被篡改后会解析失败。
            </p>
          </section>

          <section>
            <h3>文件格式</h3>
            <p>
              完整文件由 48 字节 Header、12 字节随机 Nonce、加密 Payload
              以及 16 字节 GCM Tag 组成。Header 中仅保存 key_id，
              不会泄露任何敏感字段。
            </p>
          </section>

          <section>
            <h3>使用流程</h3>
            <ol>
              <li>在左侧选择「参数构建」页面。</li>
              <li>编辑 72 个参数（地址 0~35 在左栏，36~71 在右栏）。</li>
              <li>点击「一键校验」检查参数完整性。</li>
              <li>校验通过后点击「生成加密 bin」导出文件。</li>
              <li>需要验证时，切换到「参数解析」页面加载 bin 文件。</li>
            </ol>
          </section>

          <section>
            <h3>注意事项</h3>
            <ul>
              <li>参数地址固定为 0~71，不可增删。</li>
              <li>参数名称最多 30 个字符。</li>
              <li>默认值范围为 0~65535。</li>
              <li>隐藏参数在 ESP32 客户界面中应不可见。</li>
              <li>工程文件是内部明文文件，发布给 ESP32 的文件应使用加密 bin。</li>
            </ul>
          </section>
        </div>

        <div className="about-footer">
          <button onClick={onClose}>我知道了</button>
        </div>
      </div>
    </div>
  );
};
