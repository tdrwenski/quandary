%-*-octave-*--
%
% USAGE:  f = if1(t, pcof)
%
% INPUT:
% t: time (real scalar)
% pcof: coefficient
%
% OUTPUT:
%
% f: imaginary part of time function at time t
%
function  [f] = if1(t, pcof)
  D = size(pcof,1);
  f = 0;
# Final time T
  global T;
# Frequency
  d1 = 24.64579437;
  
  tp = T;
  t0 = 0.5*T;
  
  tau = (t - t0)/tp;
  mask = (tau >= -0.5 & tau <= 0.5);

  if (D>=2)
    f = pcof(2) * 64*mask.*(0.5 + tau).^3 .* (0.5 - tau).^3 .*sin(d1*t);
  end
  
end
