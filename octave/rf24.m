%-*-octave-*--
%
% USAGE:  f = rf24(t, pcof)
%
% INPUT:
% t: time (real scalar)
% pcof: coefficient
%
% OUTPUT:
%
% f: real part of time function at time t
%
function  [f] = rf24(t, pcof)
  D = size(pcof,1);
  if (D != 24)
    printf("ERROR: rf24 only works when D=24!\n");
    f=-999;
    return;
  end
  f = 0;

# Final time T
  global T;
# Frequency
# coefficients in H0
  d_omega = zeros(1,3);
  d_omega(1) = 24.64579437;
  d_omega(2) = 47.88054868 - 24.64579437;
  d_omega(3) = 69.70426293 - 47.88054868;

  # base wavelet
  tp = T;
  tc = 0.5*T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5) .* (0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + pcof(1) * envelope .*cos(d_omega(1)*t);
# state 2 to 3
  f = f + pcof(3) * envelope .*cos(d_omega(2)*t);
# state 3 to 4
  f = f + pcof(5) * envelope .*cos(d_omega(3)*t);

# period T/2 wavelets, centered at (0.25, 0.5, 0.75)*T
  tp = 0.5*T;

  tc = 0.25*T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5).*(0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + pcof(7) * envelope .*cos(d_omega(1)*t);
# state 2 to 3
  f = f + pcof(9) * envelope .*cos(d_omega(2)*t);
# state 3 to 4
  f = f + pcof(11) * envelope .*cos(d_omega(3)*t);


  tc = 0.5*T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5).*(0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + pcof(13) * envelope .*cos(d_omega(1)*t);
# state 2 to 3
  f = f + pcof(15) * envelope .*cos(d_omega(2)*t);
# state 3 to 4
  f = f + pcof(17) * envelope .*cos(d_omega(3)*t);

  tc = 0.75*T;
  tau = (t - tc)/tp;
  envelope = 64*(tau >= -0.5 & tau <= 0.5).*(0.5 + tau).^3 .* (0.5 - tau).^3;
# from state 1 (ground) to state 2
  f = f + pcof(19) * envelope .*cos(d_omega(1)*t);
# state 2 to 3
  f = f + pcof(21) * envelope .*cos(d_omega(2)*t);
# state 3 to 4
  f = f + pcof(23) * envelope .*cos(d_omega(3)*t);
  
end
