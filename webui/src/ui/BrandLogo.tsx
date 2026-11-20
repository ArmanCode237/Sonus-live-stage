// Sonus Live Stage logo (same drawing as resources/logo/sonus-logo.svg): a sound wave
// over keyboard keys, on a warm stage-light badge.
import { useId } from 'react';

export function BrandLogo({ size = 32, className = '' }: { size?: number; className?: string }) {
  const id = `sonus-bg-${useId().replace(/:/g, '')}`;
  return (
    <svg className={`brand-logo ${className}`} viewBox="0 0 64 64" width={size} height={size} aria-hidden="true" focusable="false">
      <defs>
        <linearGradient id={id} x1="0" y1="0" x2="1" y2="1">
          <stop offset="0" stopColor="#ef3b55" />
          <stop offset="1" stopColor="#f6a23a" />
        </linearGradient>
      </defs>
      <rect x="2" y="2" width="60" height="60" rx="15" fill={`url(#${id})`} />
      <rect x="2" y="2" width="60" height="60" rx="15" fill="none" stroke="#fff" strokeOpacity=".28" strokeWidth="1.5" />
      <path d="M11 25 C15.5 12 20.5 12 25 25 S34.5 38 39 25 S48.5 12 53 25" fill="none" stroke="#fff" strokeWidth="4.6" strokeLinecap="round" />
      <rect x="11" y="37" width="13" height="17" rx="2.5" fill="#fff" />
      <rect x="25.5" y="37" width="13" height="17" rx="2.5" fill="#fff" />
      <rect x="40" y="37" width="13" height="17" rx="2.5" fill="#fff" />
      <rect x="20.5" y="37" width="7.5" height="10" rx="1.6" fill="#1b1d22" />
      <rect x="36" y="37" width="7.5" height="10" rx="1.6" fill="#1b1d22" />
    </svg>
  );
}
