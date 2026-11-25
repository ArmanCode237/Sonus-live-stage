// Sonus Live Stage logo (design team, resources/logo/sonus-logo-2.png without the version):
// 256 px copy in src/assets, bundled by Vite with the console.
import logoUrl from '../assets/sonus-logo.png';

export function BrandLogo({ size = 32, className = '' }: { size?: number; className?: string }) {
  return (
    <img
      className={`brand-logo ${className}`}
      src={logoUrl}
      width={size}
      height={size}
      alt=""
      aria-hidden="true"
      draggable={false}
      style={{ borderRadius: Math.round(size * 0.22), display: 'block' }}
    />
  );
}
