%-*-octave-*--
%
% plotunitary: plot traces of the solution of the schroedinger equaiton
%
% USAGE:
% 
% plotunitary()
%
% INPUT:
%
function usave = plotunitary(us, mode)
  if nargin < 1
    printf("ERROR: no solution provided\n");
    return;
  end

  if nargin < 2
    mode=0; # abs
  end

  T=20;
  nsteps = length(us(1,1,:));
  
  N1 = length(us(:,1,1));
  N2 = length(us(1,:,1));
#    printf("Data has dimensions %d x %d x %d\n", N1, N2, nsteps);

  if (N1 != N2)
    printf("ERROR: N1=%d and N2=%d must be equal!\n");
    return;
  end

  t = linspace(0,T,nsteps);

# one figure for the response of each basis vector
  for q=1:N2
    figure(q);
    if (mode==1)
      h=plot(t, real(us(1,q,:)), t, real(us(2,q,:)), t, real(us(3,q,:)), t, real(us(4,q,:)));
      legend("Re(u0)", "Re(u1)", "Re(u2)", "Re(u3)", "location", "east");
    else
      h=plot(t, abs(us(1,q,:)).^2, t, abs(us(2,q,:)).^2, t, abs(us(3,q,:)).^2, t, abs(us(4,q,:)).^2);
      legend("|u0|^2", "|u1|^2", "|u2|^2", "|u3|^2", "location", "east");
    end
    axis tight;
    set(h,"linewidth",2);
    tstr = sprintf("Response to initial data e%1d", q-1);
    title(tstr);
    xlabel("Time");
  end
    
end
