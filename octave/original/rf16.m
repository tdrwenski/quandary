%-*-octave-*--
%
% USAGE:  f = rf16(t, param)
%
% INPUT:
% t: time (real scalar)
% params: struct containing (pcof, T, d_omega) 
%
% OUTPUT:
%
% f: real part of time function at time t
%
function  [f] = rf16(t, param)
  D = size(param.pcof,1);
  if (D != 16)
    printf("ERROR: rf16 only works when pcof has 16 elements\n");
    f=-999;
    return;
  end
  f = 0;

  # base wavelet
  tp = param.T;
  tc = 0.5*param.T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5) .* (0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + param.pcof(1) * envelope .*cos(param.d_omega(1)*t);
# state 2 to 3
  f = f + param.pcof(2) * envelope .*cos(param.d_omega(2)*t);
# state 3 to 4
  f = f + param.pcof(3) * envelope .*cos(param.d_omega(3)*t);
# state 4 to 5
  f = f + param.pcof(4) * envelope .*cos(param.d_omega(4)*t);

# period T/2 wavelets, centered at (0.25, 0.5, 0.75)*T
  tp = 0.5*param.T;

  tc = 0.25*param.T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5).*(0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + param.pcof(5) * envelope .*cos(param.d_omega(1)*t);
# state 2 to 3
  f = f + param.pcof(6) * envelope .*cos(param.d_omega(2)*t);
# state 3 to 4
  f = f + param.pcof(7) * envelope .*cos(param.d_omega(3)*t);
# state 4 to 5
  f = f + param.pcof(8) * envelope .*cos(param.d_omega(4)*t);


  tc = 0.5*param.T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5).*(0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + param.pcof(9) * envelope .*cos(param.d_omega(1)*t);
# state 2 to 3
  f = f + param.pcof(10) * envelope .*cos(param.d_omega(2)*t);
# state 3 to 4
  f = f + param.pcof(11) * envelope .*cos(param.d_omega(3)*t);
# state 4 to 5
  f = f + param.pcof(12) * envelope .*cos(param.d_omega(4)*t);

  tc = 0.75*param.T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5).*(0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + param.pcof(13) * envelope .*cos(param.d_omega(1)*t);
# state 2 to 3
  f = f + param.pcof(14) * envelope .*cos(param.d_omega(2)*t);
# state 3 to 4
  f = f + param.pcof(15) * envelope .*cos(param.d_omega(3)*t);
  # state 4 to 5
  f = f + param.pcof(16) * envelope .*cos(param.d_omega(4)*t);

end