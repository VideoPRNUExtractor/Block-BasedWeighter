%% PRNU weighting with depending on QP value of macro blocks.
%% This code must be used with FFMPEG and Extractor in
%% https://github.com/VideoPRNUExtractor/Weighter

%% The fingerprints have to be extracted according to Goljan, Fridrich
%% http://dde.binghamton.edu/download/camera_fingerprint/

%%   INPUT
%%   videoPath   : video file path with its name
%%   extractor   : path of extractor
%%   weight :  QP values to their weightes. 1x51 array, every elements of array
%%                         represent weight of equivalent QP.
%%                         It can be default value or can be given by calculating for specific camera.

%%   OUTPUT
%%   fingerprint        : fingerprint optained with weighted PRNU.
%%

function [fingerprint] =getFingerprintWeighted(videoPath,extractor,weight)
if nargin<3
    weight = [2.00906240291676,1.99605141807694,1.96349376087940, ....
        1.95998649475040,1.93163880139253,1.90562738507855, ....
        1.87629388821831,1.83047857243608,1.78310147038273, ....
        1.74941859993895,1.70854447960799,1.63695561830877, ....
        1.53799991385559,1.38484048744275,1.22013186856014, ....
        1.01477178567884,0.840285484004224,0.706489977338469, ....
        0.607463940302274,0.493627014144769,0.419161810824988, ....
        0.344591636861239,0.289763458583624,0.248742016626542, ....
        0.213051191508839,0.168077858985827,0.128430994647728, ....
        0.113203331685946,0.0957111511471863,0.0860128789712184,  ....
        0.0729325326344996,0.0685262264934336,0.0643031182758743,  ....
        0.0612578077471137,0.0524890694902743,0.0453645053360809,  ....
        0.0419982028367397,0.0293467847633734,0.0381813046792846,  ....
        0.0240844061993151,0.0221425774784442,0.0277328455598480,  ....
        0.0217154342546012,0.0155701820534147,0.0211523612880064,  ....
        0.0290842671704966,0,0.0197250641331140,0.0189467491564214, ....
        0,0.0152787861210515];
end

current=cd(extractor);
%binary file depends on its location. Therefor first change to directory
system(cell2mat(strcat('./extract_mvs',{' '},videoPath,{' '},'1')))
%extractor runing
cd(current);
%back to old directory


L = 4;         
qmf = MakeONFilter('Daubechies',8);

%getting resolution of image
[M,N,~]=size(imread(strcat(videoPath,'Frame/frame-0001.jpg')));

for j=1:3
    RPsum{j}=zeros(M,N,'single');
    NN{j}=zeros(M,N,'single');
end
sumNoisex=zeros(M,N);
sumIx=ones(M,N);

conn = database(strcat(videoPath,'db.db'),'','','org.sqlite.JDBC',strcat('jdbc:sqlite:',videoPath,'db.db'));
tablo = fetch(conn,'select FrameID, MBx, MBy, MBw, MBh, qp from MBs');
frameId=cell2mat(tablo(:,1));
x=cell2mat(tablo(:,2));
y=cell2mat(tablo(:,3));
w=cell2mat(tablo(:,4));
h=cell2mat(tablo(:,5));
qp=cell2mat(tablo(:,6));

qp(qp<1) = 1;
%if QP value not setted it is setted to 1
clear tablo
%if video contains more frame or computer RAM is low may be this
%process give out of memory. This case select request can be diveded
%multiple part

counter=frameId(1);
temp=frameId(1);
i=1;
mask=zeros(M,N);
Length=length(frameId);
for row=1:Length
    if(temp~=frameId(row))
        imx=createPath(strcat(videoPath,'Frame/frame'),counter);
        try
            Ximage=imread(imx);
        catch
            disp('frame Read Error')
        end
        X = double255(Ximage);
        
        for j=1:3
            ImNoise = single(NoiseExtract(X(:,:,j),qmf,3,L));
            Inten = single(IntenScale(X(:,:,j))).*Saturation(X(:,:,j));
            RPsum{j} = RPsum{j}+mask.*ImNoise.*Inten;   	% weighted average of ImNoise (weighted by Inten)
            NN{j} = NN{j} + mask.*(Inten.^2);
        end
        
        mask=zeros(M,N);
        i=i+1;
        counter=counter+1;
        SeeProgress(counter),
        temp=frameId(row);
    end
    starx=x(row)+1;
    stary=y(row)+1;
    stopx=x(row)+w(row);
    stopy=y(row)+h(row);
    % it is not possible stopy bigger than M but checking it
    if(stopy>M)
        stopy=M;
        h(row)=stopy-stary+1;
    end
    % it is not possible stopx bigger than N but checking it
    if(stopx>N)
        stopx=N;
        w(row)=stopx-starx+1;
    end
    weightOfBlock=weight(qp(row));
    mask(stary:stopy, starx:stopx)=ones(h(row), w(row))*weightOfBlock;
    
end
%last frame
imx=createPath(strcat(videoPath,'Frame/frame'),counter);
try
    Ximage=imread(imx);
    X = double255(Ximage);
    
    for j=1:3
        ImNoise = single(NoiseExtract(X(:,:,j),qmf,3,L));
        Inten = single(IntenScale(X(:,:,j))).*Saturation(X(:,:,j));
        RPsum{j} = RPsum{j}+mask.*ImNoise.*Inten;   	% weighted average of ImNoise (weighted by Inten)
        NN{j} = NN{j} + mask.*(Inten.^2);
    end
catch
end

clear ImNoise Inten X

RP = cat(3,RPsum{1}./(NN{1}+1),RPsum{2}./(NN{2}+1),RPsum{3}./(NN{3}+1));
% Remove linear pattern and keep its parameters
[RP,~] = ZeroMeanTotal(RP);
RP = single(RP);
RP = rgb2gray1(RP);
sigmaRP = std2(RP);
fingerprint = WienerInDFT(RP,sigmaRP);
end

%%% FUNCTIONS %%

function [path] = createPath(pathA,i)
if(i<10)
    path=strcat(strcat(pathA,  '-000'),int2str(i),'.jpg');
elseif(i<100)
    path=strcat(strcat(pathA,  '-00'),int2str(i),'.jpg');
elseif(i<1000)
    path=strcat(strcat(pathA,  '-0'),int2str(i),'.jpg');
else
    path=strcat(strcat(pathA,  '-'),int2str(i),'.jpg');
end
end

function X=double255(X)
% convert to double ranging from 0 to 255
datatype = class(X);
switch datatype,                % convert to [0,255]
    case 'uint8',  X = double(X);
    case 'uint16', X = double(X)/65535*255;
end
end