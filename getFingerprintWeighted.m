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
    weight = [1.6384,1.5683,1.5675,1.5558,1.5508,1.5265,1.5202, 1.4988,1.4749,1.4508,1.4107,1.3993,1.3545,1.2969,1.1494,1,0.83084,0.66702, 0.55987,0.45938,0.39828,0.33566,0.32355,0.26975,0.2498,0.22692,0.22318,0.18025, 0.18791,0.17631,0.16787,0.13814,0.10502,0.096901,0.078456,0.10364,0.11001,0.11422, 0.109,0.10556,0.10906,0.1,0.09,0.084414,0.07,0.062,0.054,0.045592,0.035,0.031,0]; %QP based weight without skip blocks
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
tablo = fetch(conn,'select FrameID, MBx, MBy, MBw, MBh, qp, MBtype from MBs');
frameId=cell2mat(tablo(:,1));
x=cell2mat(tablo(:,2));
y=cell2mat(tablo(:,3));
w=cell2mat(tablo(:,4));
h=cell2mat(tablo(:,5));
qp=cell2mat(tablo(:,6));
mbType=cell2mat(tablo(:,7));

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
    if mbType(row)==-123 %skip blocks are marked with -123
        weightOfBlock=0;
    end
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
