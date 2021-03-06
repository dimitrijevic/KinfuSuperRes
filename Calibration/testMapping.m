%clear all;
close all;
clc;
addpath( '/media/Storage/workspace_ubuntu/3rdparty/toolbox_calib');

MAX_DEPTH    = 10001;
DEPTH_PREFIX = 'dep16_';
IMG_PREFIX   = 'img8_';
%IMG_PATH     = '/media/Storage/workspace_ubuntu/rec/calibTrollKinect';
%IMG_PATH     = '/media/Storage/workspace_ubuntu/rec/imgs_20130730_1638_calibPrism2/orig';
%IMG_PATH     = '/home/bontius/workspace/cpp_projects/KinfuSuperRes/SuperRes-NI-1-5/build/out/imgs_20130725_1809';
IMG_PATH     = '/home/bontius/workspace/cpp_projects/KinfuSuperRes/SuperRes-NI-1-5/build/out/imgs_20130726_1929/';
IMG_NAME     = '00000001.png';

dep8 = imread( [IMG_PATH filesep DEPTH_PREFIX IMG_NAME] );
subplot(121); imshow(dep8);
img8 = imread( [IMG_PATH filesep IMG_PREFIX IMG_NAME] );
img8 = imresize(img8,size(dep8));
subplot(122); imshow(img8);

dsize = size( dep8 );
isize = size( img8 );

addpath( 'util' );
%addpath( '/media/Storage/workspace_ubuntu/rec/calibTrollKinect' )
%CALIB_PATH = '/media/Storage/workspace_ubuntu/rec/imgs_20130730_1638_calibPrism2/';
%CALIB_PATH = '/media/Storage/workspace_ubuntu/rec/calibTrollKinect/';
CALIB_PATH = '/media/Storage/workspace_ubuntu/rec/imgs_20130805_1047_calibPrism4/';

DepthCalibPath  = [ CALIB_PATH 'Calib_Results_ir_left.mat'];
RgbCalibPath    = [ CALIB_PATH 'Calib_Results_rgb_right.mat'];
StereoCalibPath = [ CALIB_PATH 'Calib_Results_stereo_noreproj.mat'];

XOFFSET = 0;
YOFFSET = 0;

%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

dep_KK          = load( DepthCalibPath, 'KK' );

dep_KK          = dep_KK.KK;
dep_inv_KK      = load( DepthCalibPath, 'inv_KK' );
dep_inv_KK      = dep_inv_KK.inv_KK;

dep_kc          = load( DepthCalibPath, 'kc' );
dep_kc          = dep_kc.kc;
dep_alpha_c     = load( DepthCalibPath, 'alpha_c' );
dep_alpha_c     = dep_alpha_c.alpha_c;

rgb_KK          = load( RgbCalibPath, 'KK' );
rgb_KK          = rgb_KK.KK;
rgb_inv_KK      = load( RgbCalibPath, 'inv_KK' );
rgb_inv_KK      = rgb_inv_KK.inv_KK;

rgb_kc          = load( RgbCalibPath, 'kc' );
rgb_kc          = rgb_kc.kc;
rgb_alpha_c     = load( RgbCalibPath, 'alpha_c' );
rgb_alpha_c     = rgb_alpha_c.alpha_c;

om              = load( StereoCalibPath, 'om' );
om              = om.om;

R               = load( StereoCalibPath, 'R' );
R               = R.R

T               = load( StereoCalibPath, 'T' );
T               = T.T

%%%%
t_gamma = zeros(1,MAX_DEPTH);
k1 = 1.1863;
k2 = 2842.5;
k3 = 0.1236;
for i = 0 : MAX_DEPTH-1
    depth = k3 * tan(i/k2 + k1);
    %depth = 1.0 / (double(i) * -0.0030711016 + 3.3309495161);
    %depth = ((i/2047)^3) * 36 * 255;
    t_gamma(i+1) = depth;
end

figure();
%hold all;
plot(t_gamma)
%plot(disps);
%hold off;
%%%%

mapped = zeros( size(img8,1), size(img8,2) );
mapped_count = 0;
for x = 0 : 1 : dsize(2) - 1 - XOFFSET
    %x = dsize(2) / 2;
    disp(sprintf( '%f%%', x / (dsize(2)-1)*100 ));
    for y = 0 : 1 : dsize(1) - 1 - YOFFSET
         %y = dsize(1) / 2;
        
        if 1
            z = dep8( y + 1 + YOFFSET, x + 1 + XOFFSET );
            %d = t_gamma( z + 1 );
            d = double(z);
           
            if 1
%                 P_world = [ normalize( [ x, y ]', ...
%                     [ dep_KK(1,1) dep_KK(2,2) ], ...
%                     [ dep_KK(1,3) dep_KK(2,3) ], ...
%                     dep_kc, dep_alpha_c ) * d; d ];

                P_world = [ normalize( [ x, y ]', ...
                    [ dep_KK(1,1) dep_KK(2,2) ], ...
                    [ dep_KK(1,3) dep_KK(2,3) ], ...
                    dep_kc, dep_alpha_c ); 1 ] * d;
                
                
                p_rgb = project_points2( P_world,om,T, ...
                    [ rgb_KK(1,1) rgb_KK(2,2) ], ...
                    [ rgb_KK(1,3) rgb_KK(2,3) ], ...
                    rgb_kc, rgb_alpha_c );
            else
                P_world = [                            ...
                    (x - dep_KK(1,3)) * d / dep_KK(1,1), ...
                    (y - dep_KK(2,3)) * d / dep_KK(2,2), ...
                    d ]';
               
                P2 = R * P_world + T/1000;
                p_rgb = [ ...
                            P2(1) * rgb_KK(1,1) / P2(3) + rgb_KK(1,3), ...
                            P2(2) * rgb_KK(2,2) / P2(3) + rgb_KK(2,3)  ...
                        ];
                
                %p_rgb = p_rgb * double(d);
            end
        end
        
        if 0
            z = dep8(y+1, x+1);
            d = RawDepthToMeters( z / 255.0 * 2047.0 );
            
            P = DepthToWorld( x, y, z );
            p_rgb = WorldToColor( P );
        end
        
        p_rgb = round(p_rgb);
        if ( (p_rgb(1) > 0) && (p_rgb(2) > 0) && (p_rgb(1) < isize(2)) && (p_rgb(2) < isize(1)) )
            %dispr = toDisparity( d );
            %dispr = toDisparity( P2(3) );
            dispr = P_world(3);
            coords = [ round(p_rgb(2)) + 1, round(p_rgb(1)) + 1 ];
            if ( (mapped(coords(1), coords(2))) == 0 || (dispr < mapped(coords(1), coords(2))) )
                mapped( coords(1), coords(2) ) = dispr;
            end
            
            mapped_count = mapped_count + 1;
            if ( mapped_count < 100 )
                mapped_count
            end
        end
    end
end

%img8(:,:,1) = blend( uint8(mapped * 1000.0 / MAX_DEPTH * 255.0 ), img8(:,:,1) );
overlay = blend( double(img8) / 255.0, mapped / 10001.0, .3 );
imwrite( uint16(mapped), [IMG_PATH filesep DEPTH_PREFIX IMG_NAME '_mapped.png'] );

mapped_count / size(mapped,1) / size(mapped,2)

figure();
imshow(mapped, [min(mapped(:)), max(mapped(:)) ] );
figure();
myimshow( overlay );
imwrite( uint8(overlay*255.0), [IMG_PATH filesep IMG_PREFIX IMG_NAME '_mapped.png'] );